lauditd
-------

Lustre Changelog Daemon feeding a named pipe (FIFO).


Installation
------------

Building lauditd is relatively straightforward:

  $ sh ./autogen.sh
  $ ./configure
  $ make

The "rpms" target allows to build RPM package.


Output format
-------------

`lauditd` reads Lustre Changelogs from Lustre directly using the liblustreapi
and (atomically) write outputs to a named pipe (FIFO) using a key=value format
suitable for log analysis software like Splunk:

```
2020-11-05T11:08:58.998351225-0800 mdt=oak-MDT0002 id=89954723 type=CREAT flags=0x0 uid=231650 gid=99 target=[0x2f8000b442:0xcfd9:0x0] parent=[0x2f8000b458:0x4623:0x0] name="oligocalls_047_006.centr_evalknown.gz"
```

Running lauditd
---------------

`lauditd` doesn't need any configuration file as it takes all its parameters from
command line. One lauditd daemon should be used per Lustre MDT. You will need
to set up a Changelog reader ID dedicated to `lauditd`. On the MDS, run:

  $ lctl --device fsname-MDT0002 changelog_register
  fsname-MDT0002: Registered changelog userid 'cl2'

`lauditd` should be run on a Lustre client with the filesystem you want to audit
already mounted (read-only is supported).

Example of invocation with FIFO `/run/lauditd/fsname-MDT0002.changelogs`:

  $ lauditd -u cl2 -f /run/lauditd/fsname-MDT0002.changelogs -b 1000 fsname-MDT0002

You can test with:

  $ cat /run/lauditd/fsname-MDT0002.changelogs

For Linux systems using systemd, you can use the service unit file provided with
configuration file located at `/etc/sysconfig/lauditd`.

To start `lauditd`, use:

  $ systemctl start lauditd@fsname-MDT0000

To enable `lauditd` at boot time, use:

  $ systemctl enable lauditd@fsname-MDT0000


Author
------
Stephane Thiell - Stanford Research Computing Center

Please contribute by submitting a Pull Request on GitHub.