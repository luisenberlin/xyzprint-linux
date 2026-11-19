#!/usr/bin/env node
/**
 * check-gcode.mjs — valida un G-code antes de enviarlo a la da Vinci 1.0 Pro.
 *
 * No lamina ni necesita CuraEngine. Comprueba los marcadores que el formato
 * `.3w` y el firmware de esta impresora necesitan, sea cual sea la versión de
 * Cura (o cualquier otro laminador) que generó el fichero.
 *
 * Uso:
 *   node tools/check-gcode.mjs pieza.gcode
 */

import { readFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';

/** Devuelve una lista de errores y otra de avisos. */
export function checkGcode(gcode) {
    const errors = [];
    const warnings = [];
    const text = String(gcode).replace(/\r/g, '');

    // `G21 ;` con comentario es la marca que separa cabecera y G-code en el .3w.
    if (!text.includes('G21 ;'))
        errors.push('falta "G21 ;" (con comentario): xyzsend no encuentra dónde empieza el G-code');

    // El firmware de la 1.0 Pro espera estos comandos en el arranque.
    for (const command of ['M108', 'M191']) {
        if (!text.includes(command))
            errors.push(`falta ${command} en el Start G-code: la impresora puede no arrancar`);
    }
    if (!text.includes('M82'))
        warnings.push('no aparece M82 (extrusión absoluta): revisa el G-code flavor');

    const start = text.split(';LAYER')[0];
    if (/\bG91\b/.test(start))
        warnings.push('G91 en el arranque: el .3w de referencia usa coordenadas absolutas (G90)');

    return { errors, warnings };
}

/** Extrae información útil de los comentarios de cabecera. */
export function gcodeInfo(gcode) {
    const text = String(gcode).replace(/\r/g, '');
    const find = (re) => {
        const match = text.match(re);
        return match ? match[1].trim() : '';
    };
    return {
        generator: find(/^;Generated with (.+)$/m) || find(/^; generated_by = (.+)$/m),
        flavor: find(/^;FLAVOR:(.+)$/m),
        time: find(/^;TIME:(.+)$/m) || find(/^; print_time = (.+)$/m),
        filament: find(/^;Filament used: (.+)$/m)
    };
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
    const path = process.argv[2];
    if (!path) {
        console.error('uso: node tools/check-gcode.mjs <pieza.gcode>');
        process.exit(2);
    }

    let gcode;
    try {
        gcode = readFileSync(path, 'utf8');
    } catch (error) {
        console.error(`error: no se pudo leer ${path}: ${error.message}`);
        process.exit(1);
    }

    const info = gcodeInfo(gcode);
    console.log(`fichero   : ${path}`);
    if (info.generator) console.log(`laminador : ${info.generator}`);
    if (info.flavor)    console.log(`flavor    : ${info.flavor}`);
    if (info.time)      console.log(`tiempo    : ${info.time} s`);
    if (info.filament)  console.log(`filamento : ${info.filament}`);

    const { errors, warnings } = checkGcode(gcode);
    for (const warning of warnings)
        console.warn(`aviso: ${warning}`);
    for (const error of errors)
        console.error(`error: ${error}`);

    if (errors.length)
        process.exit(1);
    console.log('G-code compatible con la da Vinci 1.0 Pro');
}
