## Housekeeping

- [ ] Add a README.md
- [ ] Add a .gitignore
- [ ] Add a LICENSE.md
- [ ] Create a GitHub repo
- [ ] Learn how to create a GitHub Action that runs `make` + `make test` on PR
- [ ] Code coverage
- [ ] Add clang-tidy support

## Missing Functionality

### Features Used by Transmission


| Times Used | Current Use                    | Buffy Equivalent          |
|-----------:|:-------------------------------|---------------------------|
| 89         | evbuffer_get_length            | bfy_buffer_get_length()   |
| 54         | evbuffer_add                   | bfy_buffer_add()          |
| 44         | evbuffer_free                  | bfy_buffer_free()         |
| 37         | evbuffer_new                   | bfy_buffer_new()          |
| 37         | evbuffer_add_uint32            | bfy_buffer_add_hton_32()  |
| 31         | evbuffer_add_printf            | bfy_buffer_add_printf()   |
| 26         | evbuffer_add_uint8             | bfy_buffer_add_hton_8()   |
| 19         | evbuffer_pullup                | |
| 12         | evbuffer_free_to_str           | |
| 11         | evbuffer_drain                 | |
| 11         | evbuffer_add_hton_32           | bfy_buffer_add_hton_32()  |
| 9          | evbuffer_remove                | |
| 9          | evbuffer_read_ntoh_32          | |
| 9          | evbuffer_add_buffer            | |
| 7          | evbuffer_add_uint16            | bfy_buffer_add_hton_16()  |
| 6          | evbuffer_add_hton_64           | bfy_buffer_add_hton_64()  |
| 5          | evbuffer_iovec                 | |
| 4          | evbuffer_reserve_space         | |
| 4          | evbuffer_commit_space          | |
| 3          | tr_evbuffer_write              | |
| 3          | evbuffer_remove_buffer         | |
| 3          | evbuffer_expand                | |
| 3          | evbuffer_copyout               | |
| 3          | evbuffer_add_vprintf           | bfy_buffer_add_vprintf()  |
| 3          | evbuffer_add_uint64            | bfy_buffer_add_hton_64()  |
| 3          | evbuffer_add_reference         | |
| 2          | evbuffer_ref_cleanup_tr_free   | |
| 2          | evbuffer_read_ntoh_64          | |
| 2          | evbuffer_read                  | |
| 2          | evbuffer_ptr_set               | |
| 2          | evbuffer_ptr                   | |
| 2          | evbuffer_add_hton_16           | bfy_buffer_add_hton_16()  |
| 1          | evbuffer_write_atmost          | |
| 1          | evbuffer_search                | |
| 1          | evbuffer_peek                  | |
| 1          | evbuffer_cb_info               | |
| 1          | evbuffer_add_cb                | |
 
