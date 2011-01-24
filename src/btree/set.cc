#include "set.hpp"

#include "btree/modify_oper.hpp"

#include "buffer_cache/co_functions.hpp"

#include "btree/coro_wrappers.hpp"



struct btree_set_oper_t : public btree_modify_oper_t {

    explicit btree_set_oper_t(data_provider_t *data, set_type_t type, mcflags_t mcflags, exptime_t exptime, cas_t req_cas)
        : btree_modify_oper_t(), data(data), type(type), mcflags(mcflags), exptime(exptime), req_cas(req_cas)
    {
        pm_cmd_set.begin(&start_time);
    }

    ~btree_set_oper_t() {
        pm_cmd_set.end(&start_time);
    }


    bool operate(transaction_t *txn, btree_value *old_value, large_buf_t *old_large_value, btree_value **new_value, large_buf_t **new_large_buf) {
        try {
            if ((old_value && type == set_type_add) || (!old_value && type == set_type_replace)) {
                result = store_t::sr_not_stored;
                data->discard();
                return false;
            }

            if (type == set_type_cas) { // TODO: CAS stats
                if (!old_value) {
                    result = store_t::sr_not_found;
                    data->discard();
                    return false;
                }
                if (!old_value->has_cas() || old_value->cas() != req_cas) {
                    result = store_t::sr_exists;
                    data->discard();
                    return false;
                }
            }

            if (data->get_size() > MAX_VALUE_SIZE) {
                result = store_t::sr_too_large;
                data->discard();
                /* To be standards-compliant we must delete the old value when an effort is made to
                replace it with a value that is too large. */
                *new_value = NULL;
                *new_large_buf = NULL;
                return true;
            }

            value.value_size(0);
            if (type == set_type_cas || (old_value && old_value->has_cas())) {
                // Turns the flag on and makes
                // room. run_btree_modify_oper() will set an actual CAS
                // later. TODO: We should probably have a separate
                // function for this.
                metadata_write(&value.metadata_flags, value.contents, mcflags, exptime, 0xCA5ADDED);
            } else {
                metadata_write(&value.metadata_flags, value.contents, mcflags, exptime);
            }

            value.value_size(data->get_size());

            large_buf_t *large_value;   // May be NULL
            buffer_group_t buffer_group;

            rassert(data->get_size() <= MAX_VALUE_SIZE);
            if (data->get_size() <= MAX_IN_NODE_VALUE_SIZE) {
                large_value = NULL;
                buffer_group.add_buffer(data->get_size(), value.value());
                data->get_data_into_buffers(&buffer_group);
            } else {
                large_value = new large_buf_t(txn);
                large_value->allocate(data->get_size(), value.large_buf_ref_ptr());
                for (int64_t i = 0; i < large_value->get_num_segments(); i++) {
                    uint16_t size;
                    void *data = large_value->get_segment_write(i, &size);
                    buffer_group.add_buffer(size, data);
                }
                try {
                    data->get_data_into_buffers(&buffer_group);
                } catch (...) {
                    large_value->mark_deleted();
                    large_value->release();
                    delete large_value;
                    throw;
                }
            }

            result = store_t::sr_stored;
            *new_value = &value;
            *new_large_buf = large_value;
            return true;

        } catch (data_provider_failed_exc_t) {
            result = store_t::sr_data_provider_failed;
            return false;
        }
    }

    ticks_t start_time;

    data_provider_t *data;
    set_type_t type;
    mcflags_t mcflags;
    exptime_t exptime;
    cas_t req_cas;

    union {
        char value_memory[MAX_BTREE_VALUE_SIZE];
        btree_value value;
    };

    store_t::set_result_t result;
};


store_t::set_result_t btree_set(const btree_key *key, btree_slice_t *slice, data_provider_t *data, set_type_t type, mcflags_t mcflags, exptime_t exptime, cas_t req_cas) {
    btree_set_oper_t oper(data, type, mcflags, exptime, req_cas);
    run_btree_modify_oper(&oper, slice, key);
    return oper.result;
}
