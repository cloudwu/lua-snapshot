lua-snapshot
============

Make a snapshot for lua state to detect memory leaks.

See dump.lua for example.

Build
=====

make linux

or

make mingw (in windows)

or

make macosx

Format
======
```
if you use snapshot_utils.diff(S1,S2,pretty) with argument pretty=true,the format is below:
address+id [address]
       +type [table/function/userdata/thread]
       +tablecount [table's count]
       +source [function/thread defined location,e.g: short_src:linedefined]
       +refcount [refrence count]
       +reflist+1 refpath
               +2 refpath
               ...
refpath: is a shortest reference path,seperate by '.'
_M : main thread
_G : global table
```