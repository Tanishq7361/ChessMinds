import { defineConfig } from 'vite';
import { resolve } from 'path';
import fs from 'fs';

export default defineConfig({
  base: '/ChessMinds/',
  root: 'web',
  publicDir: 'public',
  build: {
    outDir: '../dist',
    emptyOutDir: true,
  },
  plugins: [
    {
      name: 'github-pages-postbuild',
      closeBundle() {
        const rootDir = import.meta.dirname || process.cwd();
        const distDir = resolve(rootDir, 'dist');
        const distPublic = resolve(distDir, 'public');

        // 1. Ensure dist/public exists with piece images for /public/ route compatibility
        if (!fs.existsSync(distPublic)) {
          fs.mkdirSync(distPublic, { recursive: true });
        }
        const srcPublic = resolve(rootDir, 'web', 'public');
        if (fs.existsSync(srcPublic)) {
          const files = fs.readdirSync(srcPublic);
          for (const file of files) {
            fs.copyFileSync(resolve(srcPublic, file), resolve(distPublic, file));
          }
        }

        // 2. Add .nojekyll to prevent GitHub Pages from bypassing files via Jekyll
        fs.writeFileSync(resolve(distDir, '.nojekyll'), '');

        // 3. Add 404.html fallback for GitHub Pages SPA routing
        const indexHtml = resolve(distDir, 'index.html');
        if (fs.existsSync(indexHtml)) {
          fs.copyFileSync(indexHtml, resolve(distDir, '404.html'));
        }
      }
    }
  ]
});
