## Fixable

### High importance

* Close/free the opened section handle for mapview. Both in read and write to memory. This physical memory handle has never been closed at all.


### Medium importance

* better error feedback from driver
* set the right default I/O method for 32 bit in userspace component
* Determine on runtime the correct get-info struct (32 bit / 64 bit) or alternatively refuse to run if the bitness is wrong.


### Low importance

* I'm sure there was something but obviously I forgot it due to the low importance.

--------------------------------


## Unfixable

* Iospace method bugcheck when KD is attached and when on a Hyper-V. Possibly also with VERIFIER.


## Reported / unconfirmed bugs

* PTE method successfully runs but leaves only zeros in memory dump. Confirmed in current code base, older (rc2) might not be affected?) Reason unclear.
* Memory dump may be created corrupt on Win10 1909 or higher and does not work in Volatility 3(?) Unknown which methods or driver version affected.
