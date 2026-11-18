#!/usr/bin/env node
/**
 * threew.mjs — referencia ejecutable del formato .3w (ruta da Vinci 1.0 Pro).
 *
 * Reconstruido por interoperabilidad y validado byte a byte contra ficheros
 * .3w reales. Ver docs/formato-3w.md.
 *
 * Uso:
 *   node tools/threew.mjs encode entrada.gcode salida.3w [clave]
 *   node tools/threew.mjs decode entrada.3w salida.gcode [clave]
 *   node tools/threew.mjs inspect entrada.3w
 *
 * Clave por defecto: "@xyzprinting.com@xyzprinting.com" (32 bytes, AES-256).
 * El DLL usa AES-256-ECB sin IV y relleno PKCS#7.
 */

import { createCipheriv, createDecipheriv } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';

export const MAGIC = Buffer.from('3DPFNKG13WTW', 'ascii');
export const HEADER_SIZE = 0x2000;
export const DEFAULT_KEY = Buffer.from('@xyzprinting.com@xyzprinting.com', 'ascii');

// Tabla nibble estándar CRC-32 (polinomio 0xEDB88320), idéntica a DAT_1800117b0.
const CRC_TABLE = new Uint32Array([
  0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
  0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
]);

/** CRC-32 con init 0 y XOR final, como usa el formato .3w. */
export function crc32(buf) {
  let crc = 0xffffffff;
  for (const byte of buf) {
    crc = CRC_TABLE[(crc ^ byte) & 0xf] ^ (crc >>> 4);
    crc = CRC_TABLE[(crc & 0xf) ^ (byte >>> 4)] ^ (crc >>> 4);
  }
  return (~crc) >>> 0;
}

function aesEcb(key, encrypt) {
  const algo = key.length === 32 ? 'aes-256-ecb' : 'aes-128-ecb';
  const c = encrypt ? createCipheriv(algo, key, null) : createDecipheriv(algo, key, null);
  c.setAutoPadding(false);
  return c;
}

/**
 * Construye un .3w a partir del G-code en claro.
 * @param {Buffer} gcode
 * @param {Buffer} [key]
 * @returns {Buffer}
 */
export function encode(gcode, key = DEFAULT_KEY) {
  if (key.length !== 16 && key.length !== 32) {
    throw new Error(`clave de ${key.length} bytes: se esperaban 16 o 32`);
  }
  const g21 = gcode.indexOf('G21 ;');
  if (g21 < 0) throw new Error('no se encontró "G21 ;" en el G-code');

  // El DLL cifra ceil(n/16)*16 bytes. Si n ya es múltiplo de 16 no añade bloque.
  const encLen = Math.ceil(gcode.length / 16) * 16;
  const padded = Buffer.alloc(encLen);
  gcode.copy(padded);
  const padLen = encLen - gcode.length;
  if (padLen > 0) padded.fill(padLen, gcode.length);

  const cipher = aesEcb(key, true);
  const payload = Buffer.concat([cipher.update(padded), cipher.final()]);

  const header = Buffer.alloc(HEADER_SIZE);
  MAGIC.copy(header, 0);
  header[0x0c] = 0x01; // versión 1.5
  header[0x0d] = 0x05;
  header.writeUInt32BE(8, 0x10);
  header.write('TagE', 0x1c, 'ascii');
  header.write(key.length === 16 ? 'a128' : 'J256', 0x20, 'ascii');
  header.writeUInt32BE(g21, 0x24);       // longitud del texto de cabecera
  header.writeUInt32BE(0x44, 0x28);      // constante observada
  header.writeUInt32BE(1, 0x2c);         // constante observada
  header.writeUInt32BE(crc32(payload), 0x30);
  gcode.copy(header, 0x70, 0, g21);      // prefijo hasta "G21 ;" (exclusive)
  header[0x70 + g21] = 0;

  return Buffer.concat([header, payload]);
}

/**
 * Extrae el G-code de un .3w.
 * @param {Buffer} file
 * @param {Buffer} [key]
 * @param {{dllPadding?: boolean}} [opts] dllPadding=true replica el bug del DLL:
 *   un relleno de 1 byte no se elimina.
 * @returns {{gcode: Buffer, header: object}}
 */
export function decode(file, key = DEFAULT_KEY, opts = {}) {
  const { dllPadding = true } = opts;
  if (file.length < HEADER_SIZE) throw new Error('fichero menor que la cabecera');
  if (!file.subarray(0, 12).equals(MAGIC)) throw new Error('magic .3w incorrecto');

  const tag = file.subarray(0x20, 0x24).toString('ascii');
  const expected = tag === 'J256' ? 32 : tag === 'a128' ? 16 : 0;
  if (!expected) throw new Error(`tag de clave desconocido: ${JSON.stringify(tag)}`);
  if (key.length !== expected) {
    throw new Error(`tag ${tag} exige clave de ${expected} bytes, recibida de ${key.length}`);
  }

  const payload = file.subarray(HEADER_SIZE);
  if (payload.length === 0 || payload.length % 16 !== 0) {
    throw new Error(`longitud de payload inválida: ${payload.length}`);
  }
  const decipher = aesEcb(key, false);
  let plain = Buffer.concat([decipher.update(payload), decipher.final()]);

  const p = plain[plain.length - 1];
  let trim = 0;
  if (p < 0x11) {
    trim = p;
    if (dllPadding && p === 1) trim = 0; // comportamiento literal del DLL
    if (trim >= 2) {
      for (let i = plain.length - trim; i < plain.length; i++) {
        if (plain[i] !== plain[plain.length - trim]) { trim = 0; break; }
      }
    }
  }
  if (trim) plain = plain.subarray(0, plain.length - trim);

  return {
    gcode: plain,
    header: {
      version: `${file[0x0c]}.${file[0x0d]}`,
      textLength: file.readUInt32BE(0x24),
      field28: file.readUInt32BE(0x28),
      field2c: file.readUInt32BE(0x2c),
      crc32: file.readUInt32BE(0x30),
      crc32Ok: crc32(payload) === file.readUInt32BE(0x30),
      keyTag: tag,
      text: file.subarray(0x70, 0x70 + file.readUInt32BE(0x24)).toString('ascii'),
    },
  };
}

function main(argv) {
  const [cmd, input, output, keyArg] = argv;
  if (!cmd || !input) {
    console.error('uso: threew.mjs encode|decode|inspect <entrada> [salida] [clave]');
    process.exit(2);
  }
  const key = keyArg ? Buffer.from(keyArg, 'ascii') : DEFAULT_KEY;
  if (cmd === 'encode') {
    if (!output) throw new Error('falta la salida');
    const gcode = readFileSync(input);
    writeFileSync(output, encode(gcode, key));
    console.log(`${output}: ${gcode.length} B de G-code -> ${encode(gcode, key).length} B`);
  } else if (cmd === 'decode') {
    if (!output) throw new Error('falta la salida');
    const { gcode, header } = decode(readFileSync(input), key);
    writeFileSync(output, gcode);
    console.log(`${output}: ${gcode.length} B  crc32_ok=${header.crc32Ok}  text_len=${header.textLength}`);
  } else if (cmd === 'inspect') {
    const { header } = decode(readFileSync(input), key);
    console.log(JSON.stringify(header, null, 2));
  } else {
    throw new Error(`comando desconocido: ${cmd}`);
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  try {
    main(process.argv.slice(2));
  } catch (err) {
    console.error(`error: ${err.message}`);
    process.exit(1);
  }
}
