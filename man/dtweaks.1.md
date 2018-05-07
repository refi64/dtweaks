# dtweaks -- Automatically modify XDG desktop files according to given transforms

## SYNOPSIS

dtweaks [-i --stdin] [-r --resolve-paths] [-n --dry-run] [-v --verbose]
[APPLICATION-FILES...]

## DESCRIPTION

dtweaks is a tool to automatically modify the given XDG desktop files in accordance
with a set of "tweak files" (specified in dtweaks.desktop(5)).

Normally, you won't need to run dtweaks directly. However, it is useful to run in
order to debug tweak files.

## OPTIONS

**-i, --stdin**

> Read the list of application files to modify from standard input, separated by
newlines, **in addition** to those specified on the command line.

**-r, --resolve-paths**

> If a command line argument does not contain a slash, assume it is an application
name and automatically locate the corresponding file to process.

**-n, --dry-run**

> Instead of modifying the transformed desktop files, print their transformed contents
to the screen. Useful for debugging tweaks.

**-v, --verbose**

> Print verbose debugging information while processing tweaks.

## SEE ALSO

dtweaks.desktop(5)
