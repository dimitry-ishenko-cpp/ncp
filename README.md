# ncp – New and Improved, Now Asbestos-Free Copy Utility

```help
Usage: ncp [option]... <SOURCE>... [DESTINATION]

Options:
-A, --acl              Preserve ACL (access control list).
-a, --archive          Archive mode (equivalent to -Dfmort).
-D                     Same as --special --devices.
    --devices          Preserve device files.
-f                     Same as --unlink=force.
-g, --group            Preserve group ownership.
-H, --hard-links       Preserve hard links.
-h, --help             Show this help message and exit.
-i, --interactive      Prompt before overwriting files.
-j, --jobs=<N>         Number of files to copy in parallel (max: 16).
-L, --follow-links     Dereference source symlinks (default when non-recursive).
-M, --move             Remove source files after copying.
-m, --mode             Preserve file permissions (mode bits).
-n                     Same as --unlink=never.
-o, --ownership        Same as --user --group.
-P, --keep-links       Preserve source symlinks (default when recursive).
-p, --progress         Show progress bar.
-r, --recursive        Copy directories recursively.
    --special          Preserve named pipes and sockets.
-T, --target=<dir>     Target directory to copy into.
-t, --time             Preserve modification time.
-U                     Same as --update=older.
    --unlink=[when]    Unlink destination before writing. [when] can be one of:
                       'never', 'always', 'force' or 'auto'.
                       If [when] is omitted, 'always' is assumed.
                       If the option is omitted entirely, 'auto' is used.
    --update=[when]    Update existing files. [when] can be one of:
                       'none', 'all', 'older', 'changed' (size or time) or 'size'.
                       If [when] is omitted, 'older' is assumed.
                       If the option is omitted entirely, all files are updated,
                       which is equivalent to --update=all.
-u, --user             Preserve user ownership.
-V, --version          Show program version and exit.
-v, --verbose          Explain what is being done.

Parameters:
SOURCE                 Files or directories to copy or move.
DESTINATION            Destination file or directory.

Usage Notes:

Attributes and Permissions

    By default, ncp copies just the file contents. Owner, group, permissions,
modification time, and ACL are preserved only when requested with the --user,
--group, --mode, --time, and --acl options respectively. Otherwise, new files
receive the current time, your ownership, and default permissions (subject to
umask).

    Only root, or a process with the CAP_CHOWN capability, can change the owner
of a file. Without it, the --user option (also implied by --ownership and
--archive) has no effect and copied files are owned by you, while --group works
only for groups you belong to. If --mode is also in effect, the setuid and
setgid bits are removed from copies of files that belong to other users.

Symbolic Links

    Symlinks in the source are followed (resolved to the file they point to) when
copying non-recursively, and copied as links when copying recursively. Use the
--follow-links or --keep-links option to override this default.

    Symlinks at the destination are also followed, except when a source symlink
is copied as a link or in move mode. In those cases, the destination symlink
itself is replaced.

Recursive Mode

    Directories are only copied when the --recursive option is specified.
Following rsync's convention, a trailing slash on a source means "the contents
of". For example:

  ncp --recursive dir/ dest     Copies the contents of dir into dest.

  ncp --recursive dir  dest     Copies dir itself, as dest/dir if dest is an
                                existing directory, or as dest otherwise.

    When recursing, device files are skipped unless the --devices option is
given, and named pipes and sockets are skipped unless --special is given.

Archive Mode

    The --archive option is meant for making copies, such as backups and data
migration, where you want to preserve as much as possible, including
attributes, symlinks, special files, and so on.

    It is shorthand for -Dfmort and does not include --acl or --hard-links.

Move Mode

    The --move option (or running the program as the nmv symlink) implies
--recursive. Symlinks are moved as links and never followed. If the source and
destination are on the same filesystem, items are simply renamed; otherwise
they are copied and then removed.

Overwriting and Unlinking

    The --unlink option controls whether an existing destination is removed
before it is written:

  auto      Overwrite regular files in place, preserving their inode,
            ownership and hard links. Replace the destination only when its
            type differs from the source. This is the default.

  force     Like 'auto', but if a regular file cannot be opened for writing
            (for example, it is read-only), remove it and try again.

  never     Do not remove anything. Regular files are overwritten in place;
            a destination of a different type than the source is an error.

  always    Remove the destination and create a new one in its place.

Update Conditions

    The --update option decides what happens when the destination already
exists. Directories are not affected by this option.

    none    Never overwrite existing files.

    all     Always overwrite them. This is the default.

    changed Overwrite only if the size or modification time differs.
            Otherwise, the destination is considered up to date and its
            contents are not copied. Attributes are still updated (if
            requested) and, with --move, the source is removed.

    size    Like 'changed', but compares only the size.

    older   Overwrite only if the destination has an older modification time.
            Unlike 'changed' and 'size', a destination that is not older than
            the source is skipped entirely, including attributes.

Devices, Pipes and Sockets

    If a source given on the command line is a device, named pipe or socket,
or if the destination is one, ncp behaves like cp, reading the data from the
source and writing it to the destination.

    ncp /dev/sda disk.img    Reads the device into a regular file.
    ncp disk.img /dev/sdb    Writes the image to the device.
    ncp pipe file            Saves whatever is written to the pipe.

    This applies only to command-line arguments, never to items found while
recursing, and is not used with --move.

    CAUTION: If any of the --devices, --special, or --archive options are
specified, ncp will copy the source itself rather than its data, replacing the
destination. Likewise, with --unlink=always, a destination device will be
removed and replaced instead of being written to.

Interactive Prompts

    The --interactive option prompts before overwriting an existing
destination. Each prompt accepts 'y' (yes, the default), 'n' (no), 'a' (yes to
all remaining), 's' (skip all remaining) or 'q' (quit).

Parallel Execution

    The --jobs option allows ncp to copy several files at the same time. This
can speed up transfers on SSDs and network drives, but is unlikely to help, and
may even slow things down, on mechanical hard drives.

Exit Status

  0 Success.
  1 Invalid arguments.
  2 Interrupted by a signal.
  3 One or more items could not be copied.
  4 Copied, but some attributes could not be preserved.
```

## Installation

### Binary

Binary packages for Debian, Ubuntu, RaspberryPi and other Debian-based
distributions can be installed from the [CCCP Linux Package
Archive](https://github.com/cccp-linux/archive). Follow their instructions to
set up the archive and be sure to add the _main_ component. After that:

```shell
sudo apt install ncp
```

### From source

_TODO_

Share and enjoy.

## Authors

* **Dimitry Ishenko** - dimitry (dot) ishenko (at) (gee) mail (dot) com

## License

This project is distributed under the GNU GPL license. See the
[LICENSE.md](LICENSE.md) file for details.
