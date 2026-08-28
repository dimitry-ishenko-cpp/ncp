# ncp – New and Improved, Now Asbestos-Free Copy Utility

```help
Usage: ncp [option]... <SOURCE>... [DESTINATION]
                       
Options:               
-a, --archive              Archive mode (equivalent to -rmotD --unlink=auto).
-D                         Same as --special --devices.
    --devices              Preserve device files.
-F, --follow-dest-links    Dereference destination symlinks.
-f, --unlink=[when]        Unlink destination before writing. [when] can be one of:
                           'never', 'always' or 'auto'.
                           If [when] is omitted, 'always' is assumed.
                           If the option is omitted entirely, 'auto' is used.
-g, --group                Preserve group ownership.
-h, --help                 Show this help message and exit.
-i, --interactive          Prompt before overwriting files.
-j, --jobs=<N>             Number of files to copy in parallel (max: 16).
-L, --follow-links         Dereference source symlinks (default when non-recursive).
-m, --mode                 Preserve file permissions (mode bits).
-o, --ownership            Same as --user --group.
-P, --keep-links           Preserve source symlinks (default when recursive).
-p, --progress             Show progress bar.
-r, --recursive            Copy directories recursively.
    --special              Preserve named pipes and sockets.
-T, --target=<dir>         Target directory to copy into.
-t, --time                 Preserve modification time.
-U, --update=[when]        Update existing files. [when] can be one of:
                           'none', 'all', 'older', 'changed' (size or time) or 'size'.
                           If [when] is omitted, 'older' is assumed.
                           If the option is omitted entirely, all files are updated,
                           which is equivalent to --update=all.
-u, --user                 Preserve user ownership.
-V, --version              Show program version and exit.
-v, --verbose              Explain what is being done.
                       
Parameters:            
SOURCE                     Files or directories to copy or move.
DESTINATION                Destination file or directory.
```

## Installation

_TODO_

Share and enjoy.

## Authors

* **Dimitry Ishenko** - dimitry (dot) ishenko (at) (gee) mail (dot) com

## License

This project is distributed under the GNU GPL license. See the
[LICENSE.md](LICENSE.md) file for details.
