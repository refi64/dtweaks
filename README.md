# dtweaks

dtweaks is a simple but powerful tool that lets you modify XDG desktop application
files, and it contains a pacman hook to ensure those files stay modified across package
upgrades.

PRs to add hooks for other package managers would be appreciated!

## Building and installing

Make sure you have [Fbuild](https://github.com/felix-lang/fbuild/releases) (version
0.3 RC 2 or greater is required) and run:

```
$ fbuild install
```

## Tweak files

Say you want to edit Discord's desktop file to always pass `XDG_CURRENT_DESKTOP=Unity`
in order to enable app indicator support on GNOME. You put this inside
`/etc/dtweaks.d/Discord.desktop`:

```ini
[*]
Exec=/usr/bin/env XDG_CURRENT_DESKTOP=Unity $*
```

The `$*` will automatically be replaced with the original value for `Exec`, so this
will just end up prepending.

See `dtweaks.desktop(5)` for more information on writing tweaks files.

## Applying tweaks

If you're on Arch Linux, then you just need to reinstall the package in question. pacman
will run the included dtweaks hook, automatically applying the modifications.

No matter what distro you're on, if you want to run dtweaks manually, it's easy:

```
$ dtweaks /usr/share/applications/Discord.desktop
```

dtweaks takes a list of desktop files to modify, and it will search for and apply
tweaks to each one.

If you don't feel like passing the full path, then use `-r`:

```
$ dtweaks -r Discord
```

You can also use `-i` to read a list of files from stdin instead:

```
$ echo /usr/share/applications/Discord.desktop | dtweaks -i
```

This is what the included pacman hook uses to run dtweaks, and it's what you'll likely
use if you're writing hooks for other package managers.

If you're writing your own tweaks, you'll find two more options interesting: `-n` and
`-v`. `-n` will run any tweaks, but it will print the results to the screen *instead*
of writing them to disk. This is useful for checking how tweaks will be applied without
actually changing anything yet.

`-v` will print verbose information when applying tweaks. You can also set
`G_MESSAGES_DEBUG=all` before running dtweaks.

Of course, these can be combined however you want:

```
$ echo Discord | dtweaks -inrv
```
