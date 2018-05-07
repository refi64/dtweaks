from fbuild.builders.pkg_config import PkgConfig
from fbuild.builders.c import guess
from fbuild.builders import find_program

from fbuild.target import register
from fbuild.record import Record
from fbuild.path import Path
import fbuild.temp
import fbuild.db

import difflib
import os


class MrkdBuilder(fbuild.db.PersistentObject):
    def __init__(self, ctx, exe=None):
        self.ctx = ctx
        self.mrkd = exe or find_program(ctx, ['mrkd'])

    @fbuild.db.cachemethod
    def convert(self, src: fbuild.db.SRC, *, index = None, format) \
            -> fbuild.db.DST:
        assert format in ('roff', 'html')

        src = Path(src)
        ext = {'roff': '', 'html': '.html'}[format]
        dst = self.ctx.buildroot / src.basename().replaceext(ext)

        cmd = [self.mrkd, src, dst, '-format', format]
        if index is not None:
            cmd.extend(['-index', index])
            self.ctx.db.add_external_dependencies_to_call(srcs=(index,))

        self.ctx.execute(cmd, 'mrkd', '%s -> %s' % (src, dst), color='yellow')
        self.ctx.db.add_external_dependencies_to_call(dsts=[dst])
        return dst


def arguments(parser):
    group = parser.add_argument_group('config options')
    group.add_argument('--cc', help='Use the given C compiler')
    group.add_argument('--cflag', help='Pass the given flag to the C compiler',
                       action='append', default=[])
    group.add_argument('--disable-color', help="Don't force C compiler colored output",
                       action='store_true', default=False)
    group.add_argument('--release', help='Build in release mode',
                       action='store_true', default=False)
    group.add_argument('--pkg-config', help='Use the given pkg-config executable')
    group.add_argument('--destdir', help='Set the installation destdir', default='/')
    group.add_argument('--prefix', help='Set the installation prefix', default='usr')
    group.add_argument('--mrkd', help='Use the given mrkd executable')


@fbuild.db.caches
def run_pkg_config(ctx, package):
    pkg = PkgConfig(ctx, package, exe=ctx.options.pkg_config)

    try:
        ctx.logger.check('checking for %s' % package)
        cflags = pkg.cflags()
        ldlibs = pkg.libs()
    except fbuild.ExecutionError:
        ctx.logger.failed()
        raise fbuild.ConfigFailed('%s is required.' % package)

    ctx.logger.passed()
    return Record(cflags=cflags, ldlibs=ldlibs)


@fbuild.db.caches
def configure(ctx):
    flags = ctx.options.cflag
    posix_flags = ['-Wall', '-Werror']
    clang_flags = []

    if not ctx.options.disable_color:
        posix_flags.append('-fdiagnostics-color')
    if ctx.options.release:
        debug = False
        optimize = True
    else:
        debug = True
        optimize = False
        clang_flags.append('-fno-limit-debug-info')

    c = guess.static(ctx, exe=ctx.options.cc, flags=flags,
                     debug=debug, optimize=optimize, platform_options=[
                        ({'clang'}, {'flags+': clang_flags}),
                        ({'posix'}, {'flags+': posix_flags,
                                     'external_libs+': ['dl']}),
                     ])

    glib = run_pkg_config(ctx, 'glib-2.0')
    gio = run_pkg_config(ctx, 'gio-2.0')

    try:
        mrkd = MrkdBuilder(ctx, ctx.options.mrkd)
    except fbuild.ConfigFailed:
        mrkd = None

    return Record(c=c, glib=glib, gio=gio, mrkd=mrkd)


def build(ctx):
    rec = configure(ctx)
    ctx.install_destdir = ctx.options.destdir
    ctx.install_prefix = ctx.options.prefix

    dtweaks = rec.c.build_exe('dtweaks', ['src/dtweaks.c'],
                              cflags=rec.glib.cflags + rec.gio.cflags,
                              ldlibs=rec.glib.ldlibs + rec.gio.ldlibs)

    ctx.install(dtweaks, 'bin')
    ctx.install('misc/dtweaks.hook', 'share/libalpm/hooks')

    if rec.mrkd is not None:
        roff = []
        for man in Path.glob('man/*.md'):
            roff = rec.mrkd.convert(man, format='roff')
            rec.mrkd.convert(man, format='html', index='man/index.ini')
            ctx.install(roff, 'share/man/man%s' % roff.ext[1:])

    return Record(dtweaks=dtweaks, **rec)


def run_test(ctx, rec, name):
    ctx.logger.check(' * test\t%s' % name, color='cyan')

    test_base = 'tests' / name
    test_input = test_base.replaceext('.input.desktop')
    test_tweaks = test_base.replaceext('.tweaks.desktop')
    test_expected = test_base.replaceext('.expected.desktop')

    with fbuild.temp.tempdir(delete=True) as tmp:
        env = os.environ.copy()
        env['DTWEAKS_PATH'] = Path.getcwd() / 'tests'

        tmp_desktop = (tmp / name).replaceext('.tweaks.desktop')
        test_input.copy(tmp_desktop)

        rec.c.run([rec.dtweaks, tmp_desktop], env=env, quieter=1)

        with open(tmp_desktop) as fp:
            results = fp.read()
        with open(test_expected) as fp:
            expected = fp.read()

        if results != expected:
            ctx.logger.failed()
            diff = difflib.unified_diff(expected.splitlines(), results.splitlines())
            printing = False
            for line in diff:
                if not line.strip():
                    continue
                elif line.startswith('@@'):
                    printing = True
                elif printing:
                    line = ' %s %s\n' % (line[0], line[1:])
                    if line.startswith(' +'):
                        ctx.logger.write(line, color='green')
                    elif line.startswith(' -'):
                        ctx.logger.write(line, color='red')
                    else:
                        ctx.logger.write(line, color='white')
        else:
            ctx.logger.passed()


@register()
def test(ctx):
    rec = build(ctx)

    for test in Path.glob('tests/*.input.desktop'):
        run_test(ctx, rec, test.replaceext('').replaceext('').basename())
