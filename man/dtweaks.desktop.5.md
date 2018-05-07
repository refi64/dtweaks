# dtweaks.desktop -- dtweaks .desktop transformer format

## SYNOPSIS

application.desktop

## DESCRIPTION

dtweaks tweak files describe a series of modifications to be made to XDG desktop files
whenever a package is updated. The files may be placed in either /etc/dtweaks.d,
/usr/local/share/dtweaks.d, or /usr/share/dtweaks.d, in order of highest priority
to lowest priority. For more information, see dtweaks(1).

## BASIC SYNTAX

Tweak files are identical syntax-wise to XDG desktop files.

Sections are glob(7) patterns. When dtweaks runs on an XDG desktop file, it will
apply any of the tweak file's transformations whose sections match a section of the
XDG desktop file. For example, if in your tweak file, you have:

```ini
[Desktop Open*]
# Transformations here...
```

Any transformations will be run on sections of the XDG desktop file whose section title
matches the glob(7) pattern `Desktop Open*`.

## TRANSFORMATION SYNTAX

Transformations are specified by keys listed inside the tweak file. Any key in the
desktop file will be replaced by the key with the same name in the tweak file
(provided that the section matches of course). For instance, if the desktop file has
`A=1` and the tweak file has `A=2`, the desktop file will be changed to read `A=2`.

Replacement values may also use variables, which are specified by `$varname` or
`${varname}`. By default, the only variable is `$*`, which contains the value of the
key in the original desktop file. Reusing the above example, if the tweaks file instead
said `A=$* 2`, then the file will be changed to `A=1 2`.

If a key is specified again, except ending with a dollar sign (`A$=value`), then the
value will be interpreted as a regular expression that will be matched against the
value in the desktop file. Any groups matched by the regex will be available inside the
replacement value as `$group-number` and `$group-name`.

## EXAMPLE

```ini
# These transformations will be run on any section matching the glob 'Desktop Open*'.
[Desktop Open*]
# The Icon key inside the desktop file will be overwritten with this icon.
Icon=/usr/share/icons/hicolor/scalable/apps/mpv.svg
# The Comment will be rewritten, so 'Comment=something' becomes
# 'Comment=something (modified version)'.
Comment=$* (modified version)
# As mentioned above, this could be ${*} instead of $*.

# This demonstrates the regex matching capabilities.
Exec=$1 --some-other-argument=$arg
Exec$=(\w+) --some-argument=(?<arg>\S+)
```

## SEE ALSO

glob(7), dtweaks(1)
