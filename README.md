Fuse Zip
========

A zip file system implementation using libfuse and libzip.

How to run
==========

```
$ make
$ ./fusezip <zipfile> [options] <mountpoint>
```

Tips for Debugging
==================
```
$ ./fusezip <zipfile> -s -f <mountpoint>
```

```
  -s runs in single threaded mode  
  -f displays debugging output to stdout  
```

References
==========

https://github.com/libfuse/libfuse/blob/master/example/hello.c  
http://www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/html/  
https://www.cs.hmc.edu/~geoff/classes/hmc.cs135.201109/homework/fuse/fuse_doc.html  
https://github.com/NatTuck/fogfs/  
