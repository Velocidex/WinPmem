# Updates and changes

25. July 2023:

* Enabled using a foreign CR3 in the VTOP translation service. Leave it to zero for default CR3. (See linpmem_shared.h, struct `LINPMEM_VTOP_INFO`, field `associatedCR3`). Use at your own risk!
* Foreign CR3's can currently be acquired by inserted a thread into another process in gentlemen agreement, and calling Linpmem for CR3 query from there. There is no extra service.
* Made precompiler switches consistent. Commenting a precompiler switch always disables it.

25. July 2023:

* Initial commit
