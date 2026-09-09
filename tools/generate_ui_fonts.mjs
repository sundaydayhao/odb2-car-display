import { readFileSync, mkdirSync, copyFileSync, readdirSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const lvgl = resolve(root, 'managed_components/lvgl__lvgl');
const output = resolve(root, 'main/fonts');
const source = readFileSync(resolve(root, 'main/dashboard_text.h'), 'utf8');
const strings = [...source.matchAll(/^#define\s+TXT_\w+\s+(".*")$/gm)]
    .map(match => JSON.parse(match[1]));
const serviceText = readdirSync(resolve(root, 'main')).filter(name => /\.[ch]$/.test(name))
    .map(name => readFileSync(resolve(root, 'main', name), 'utf8')).join('');
const symbols = [...new Set(strings.join('') + '°' + [...serviceText].filter(char => char.charCodeAt(0) > 127).join(''))]
    .sort().join('');
mkdirSync(output, { recursive: true });

function convert(name, size, font, extra) {
    const args = ['--yes', 'lv_font_conv@1.5.3', '--font', font, '--size', String(size),
        '--bpp', '4', '--format', 'lvgl', '--no-compress', '--no-kerning',
        '--lv-include', 'lvgl.h', ...extra, '-o', resolve(output, `${name}.c`)];
    const npmCli = resolve(dirname(process.execPath), 'node_modules/npm/bin/npx-cli.js');
    const result = spawnSync(process.execPath, [npmCli, ...args], { stdio: 'inherit', cwd: root });
    if (result.status !== 0) process.exit(result.status || 1);
}

for (const size of [18, 22]) {
    convert(`dashboard_text_${size}`, size,
        resolve(lvgl, 'tests/src/test_files/fonts/noto/NotoSansSC-Regular.ttf'),
        ['--range', '0x20-0x7e', '--symbols', symbols]);
}
convert('dashboard_digits_144', 144,
    resolve(lvgl, 'scripts/built_in_font/Montserrat-Medium.ttf'), ['--symbols', '0123456789-']);
convert('dashboard_digits_72', 72,
    resolve(lvgl, 'scripts/built_in_font/Montserrat-Medium.ttf'), ['--symbols', '0123456789.-']);
copyFileSync(resolve(lvgl, 'tests/src/test_files/fonts/noto/OFL.txt'), resolve(output, 'Noto-OFL.txt'));
console.log(`Generated UI fonts with ${symbols.length} non-ASCII characters and printable ASCII.`);
