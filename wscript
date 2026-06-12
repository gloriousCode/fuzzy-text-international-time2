import shutil
import subprocess

top = '.'
out = 'build'

def run_jshint():
    jshint = shutil.which('jshint')
    if jshint:
        subprocess.check_call([jshint, '--config', 'pebble-jshintrc', 'src/js/pebble-js-app.js'])

def options(ctx):
    ctx.load('pebble_sdk')

def configure(ctx):
    ctx.load('pebble_sdk')

def build(ctx):
    ctx.load('pebble_sdk')

    run_jshint()
    binaries = []

    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.set_env(ctx.all_envs[platform])
        ctx.set_group(ctx.env.PLATFORM_NAME)
        ctx.env.append_value('LINKFLAGS', '-Wl,--no-warn-rwx-segments')
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pbl_program(source=ctx.path.ant_glob('src/**/*.c'),
                        target=app_elf)
        binaries.append({'platform': platform, 'app_elf': app_elf})

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob('src/js/**/*.js'))
