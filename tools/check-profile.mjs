#!/usr/bin/env node
/**
 * check-profile.mjs — valida el perfil de CuraEngine de la da Vinci 1.0 Pro.
 *
 * No lamina ni necesita CuraEngine: comprueba el JSON, las dimensiones, los
 * placeholders del G-code de arranque/fin y los marcadores que el `.3w`
 * necesita. Sirve para detectar errores antes de compilar en Linux.
 *
 * Uso:
 *   node tools/check-profile.mjs [profiles/davinci-1.0-pro-pla.json]
 */

import { readFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';

export const REQUIRED_GLOBAL = {
    machine_width: 200,
    machine_depth: 200,
    machine_height: 200,
    machine_center_is_zero: false,
    machine_heated_bed: true,
    machine_extruder_count: 1,
    machine_gcode_flavor: 'RepRap (Marlin/Sprinter)',
    material_bed_temperature_layer_0: 45,
    // CuraEngine 5.x los exige explícitamente al laminar por CLI.
    roofing_layer_count: 0,
    flooring_layer_count: 0
};

export const REQUIRED_EXTRUDER = {
    machine_nozzle_size: 0.4,
    material_diameter: 1.75,
    material_print_temperature_layer_0: 210,
    retraction_amount: 4,
    retraction_speed: 30
};

const PLACEHOLDER = /\{([A-Za-z0-9_]+)\}/g;

export function checkProfile(profile) {
    const errors = [];
    const fail = (message) => errors.push(message);

    if (!profile || typeof profile !== 'object')
        return ['el perfil no es un objeto JSON'];
    if (!profile.global || typeof profile.global !== 'object')
        fail('falta el objeto "global"');
    if (!profile.extruder || typeof profile.extruder !== 'object')
        fail('falta el objeto "extruder"');
    if (errors.length)
        return errors;

    for (const [key, expected] of Object.entries(REQUIRED_GLOBAL)) {
        const value = profile.global[key];
        if (value === undefined)
            fail(`falta global.${key}`);
        else if (typeof expected === 'number' && Number(value) !== expected)
            fail(`global.${key} = ${value}; se esperaba ${expected}`);
        else if (typeof expected === 'boolean' && value !== expected)
            fail(`global.${key} = ${value}; se esperaba ${expected}`);
        else if (typeof expected === 'string' && value !== expected)
            fail(`global.${key} = ${value}; se esperaba "${expected}"`);
    }

    for (const [key, expected] of Object.entries(REQUIRED_EXTRUDER)) {
        const value = profile.extruder[key];
        if (value === undefined)
            fail(`falta extruder.${key}`);
        else if (Number(value) !== expected)
            fail(`extruder.${key} = ${value}; se esperaba ${expected}`);
    }

    const start = profile.global.machine_start_gcode;
    const end = profile.global.machine_end_gcode;
    if (typeof start !== 'string' || start.length === 0)
        fail('falta global.machine_start_gcode');
    if (typeof end !== 'string' || end.length === 0)
        fail('falta global.machine_end_gcode');
    if (errors.length)
        return errors;

    for (const marker of ['G21 ;', 'M107', 'M108', 'M140 S', 'M190 S',
                          'M104 S', 'M109 S', 'M191', 'G90', 'M82']) {
        if (!start.includes(marker))
            fail(`machine_start_gcode no contiene "${marker}"`);
    }
    if (!end.includes('M84'))
        fail('machine_end_gcode no contiene "M84"');
    if (!end.includes('E-4.00000'))
        fail('machine_end_gcode no retrae 4 mm antes de M84');
    if (end.includes('G91'))
        fail('machine_end_gcode no debe usar G91 (el .3w real no lo usa)');

    const merged = { ...profile.global, ...profile.extruder };
    for (const [name, text] of [['machine_start_gcode', start],
                                ['machine_end_gcode', end]]) {
        const unresolved = [];
        for (const match of text.matchAll(PLACEHOLDER)) {
            if (!(match[1] in merged))
                unresolved.push(match[1]);
        }
        if (unresolved.length)
            fail(`${name}: placeholders sin valor: ${unresolved.join(', ')}`);
    }

    return errors;
}

export function loadProfile(path) {
    return JSON.parse(readFileSync(path, 'utf8'));
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
    const path = process.argv[2] || 'profiles/davinci-1.0-pro-pla.json';
    let profile;
    try {
        profile = loadProfile(path);
    } catch (error) {
        console.error(`error: no se pudo leer ${path}: ${error.message}`);
        process.exit(1);
    }

    const errors = checkProfile(profile);
    if (errors.length) {
        for (const error of errors)
            console.error(`error: ${error}`);
        process.exit(1);
    }

    console.log(`perfil OK: ${profile.name || path}`);
    console.log(`  cama: ${profile.global.machine_width}x${profile.global.machine_depth}x${profile.global.machine_height} mm`);
    console.log(`  boquilla: ${profile.extruder.machine_nozzle_size} mm, filamento: ${profile.extruder.material_diameter} mm`);
    console.log(`  temperaturas: ${profile.extruder.material_print_temperature_layer_0}/${profile.global.material_bed_temperature_layer_0} C`);
    console.log(`  retraccion: ${profile.extruder.retraction_amount} mm a ${profile.extruder.retraction_speed} mm/s`);
}
