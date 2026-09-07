import fs from 'node:fs';
import path from 'node:path';

const [, , inputArgument, outputArgument] = process.argv;
if (!inputArgument || !outputArgument) {
    console.error('Usage: node convert-case-model.mjs <model.gltf> <case_model.hpp>');
    process.exit(1);
}

const input = path.resolve(inputArgument);
const output = path.resolve(outputArgument);
const document = JSON.parse(fs.readFileSync(input, 'utf8'));
const buffer = fs.readFileSync(path.resolve(path.dirname(input), document.buffers[0].uri));
const primitive = document.meshes[0].primitives[0];

function readAccessor(index) {
    const accessor = document.accessors[index];
    const view = document.bufferViews[accessor.bufferView];
    const components = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4 }[accessor.type];
    const bytes = { 5123: 2, 5125: 4, 5126: 4 }[accessor.componentType];
    const stride = view.byteStride || components * bytes;
    const start = (view.byteOffset || 0) + (accessor.byteOffset || 0);
    const values = [];
    for (let item = 0; item < accessor.count; ++item) {
        const row = [];
        for (let component = 0; component < components; ++component) {
            const offset = start + item * stride + component * bytes;
            if (accessor.componentType === 5123) row.push(buffer.readUInt16LE(offset));
            else if (accessor.componentType === 5125) row.push(buffer.readUInt32LE(offset));
            else row.push(buffer.readFloatLE(offset));
        }
        values.push(components === 1 ? row[0] : row);
    }
    return values;
}

const positions = readAccessor(primitive.attributes.POSITION);
const normals = readAccessor(primitive.attributes.NORMAL);
const indices = readAccessor(primitive.indices);
if (positions.length !== normals.length || indices.length % 3 !== 0) {
    throw new Error('Unexpected mesh layout.');
}

const number = value => {
    const rounded = Math.abs(value) < 0.0000005 ? 0 : value;
    const formatted = rounded.toFixed(7).replace(/0+$/, '').replace(/\.$/, '.0');
    return `${formatted}f`;
};
const vectors = values => values.map(value => `    {${value.map(number).join(', ')}}`).join(',\n');
const indexRows = [];
for (let offset = 0; offset < indices.length; offset += 18) {
    indexRows.push(`    ${indices.slice(offset, offset + 18).join(', ')}`);
}

const header = `#pragma once

#include <array>
#include <cstdint>

namespace CaseModel {

struct Vector3 { float x, y, z; };

inline constexpr std::array<Vector3, ${positions.length}> kPositions = {{
${vectors(positions)}
}};

inline constexpr std::array<Vector3, ${normals.length}> kNormals = {{
${vectors(normals)}
}};

inline constexpr std::array<std::uint16_t, ${indices.length}> kIndices = {{
${indexRows.join(',\n')}
}};

} // namespace CaseModel
`;

fs.mkdirSync(path.dirname(output), { recursive: true });
fs.writeFileSync(output, header);
console.log(`Wrote ${positions.length} vertices and ${indices.length / 3} triangles to ${output}`);
