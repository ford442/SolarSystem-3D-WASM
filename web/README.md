# Web Directory

This directory contains web-related files for the SolarSystem 3D WebAssembly application.

## Purpose

The `index.html` and related files here provide a custom web interface for the Emscripten-generated application. This can be used as a template or for development purposes.

## Using with Emscripten Build

After building with Emscripten (using `../build-web.sh` or manual build commands), you can:

### Option 1: Use Emscripten's Default HTML
The Emscripten build generates `build-web/SolarSystem.html` which can be served directly:
```bash
cd build-web
python3 -m http.server 8000
# Open http://localhost:8000/SolarSystem.html
```

### Option 2: Use Custom HTML Template
If you want to use this directory's custom HTML:

1. Copy the generated files to this directory:
```bash
cp build-web/SolarSystem.js web/
cp build-web/SolarSystem.wasm web/
cp build-web/SolarSystem.data web/
```

2. Modify `index.html` to load the Emscripten-generated JavaScript:
```html
<script src="/SolarSystem.js"></script>
```

3. Serve this directory:
```bash
cd web
python3 -m http.server 8000
# Open http://localhost:8000
```

## Development

If you want to customize the web interface further:

1. Ensure Node.js is installed
2. Install dependencies:
```bash
cd web
npm install
```

3. Run development server (if using Vite or similar):
```bash
npm run dev
```

## Notes

- The canvas element should have the class `emscripten` for proper GLFW/Emscripten integration
- Make sure CORS is properly configured when serving files
- The application requires WebGL 2.0 support in the browser
