#
# Layer-by-Layer watchface build script (waf).
# Embeds config/index.html into the phone-side JS bundle.
#
import json
import os.path

top = '.'
out = 'build'


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    ctx.load('pebble_sdk')


def embed_config_page(ctx):
    src = ctx.path.find_node('config/index.html')
    if src is None:
        if ctx.path.find_node('src/pkjs/config_page.js') is None:
            ctx.fatal('config/index.html is missing')
        return
    text = src.read(encoding='utf-8').lstrip('﻿')
    lines = [line.strip() for line in text.splitlines()]
    html = '\n'.join(line for line in lines if line)
    js = ('// Generated from config/index.html by wscript. Do not edit.\n'
          'module.exports = ' + json.dumps(html) + ';\n')
    dst = ctx.path.make_node('src/pkjs/config_page.js')
    if not os.path.exists(dst.abspath()) or dst.read(encoding='utf-8') != js:
        dst.write(js, encoding='utf-8')


def build(ctx):
    ctx.load('pebble_sdk')
    embed_config_page(ctx)

    binaries = []
    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'), target=app_elf, bin_type='app')
        binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob(['src/pkjs/**/*.js']),
                   js_entry_file='src/pkjs/index.js')
