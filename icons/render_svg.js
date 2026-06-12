// Renders an SVG file to PNG at multiple sizes using @resvg/resvg-js
// Usage: node render_svg.js <input.svg> <output_prefix>
// Outputs: <prefix>_16.png, _24.png, _32.png, _48.png, _64.png, _128.png, _256.png

const { Resvg } = require('@resvg/resvg-js');
const fs = require('fs');
const path = require('path');

const [,, svgFile, prefix] = process.argv;
if (!svgFile || !prefix) {
    console.error('Usage: node render_svg.js <input.svg> <output_prefix>');
    process.exit(1);
}

const svgData = fs.readFileSync(svgFile, 'utf8');
const sizes = [16, 24, 32, 48, 64, 128, 256];

for (const size of sizes) {
    const resvg = new Resvg(svgData, {
        fitTo: { mode: 'width', value: size }
    });
    const png = resvg.render().asPng();
    const outFile = `${prefix}_${size}.png`;
    fs.writeFileSync(outFile, png);
    console.log(`  wrote ${outFile}  (${png.length} bytes)`);
}
