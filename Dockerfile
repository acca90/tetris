# Pixel Boy Tetris — static files served by nginx as a non-root user.
FROM nginxinc/nginx-unprivileged:stable-alpine

# Only what the browser needs; art/ holds the Aseprite sources and generator.
COPY --chown=nginx:nginx index.html /usr/share/nginx/html/
COPY --chown=nginx:nginx assets/ /usr/share/nginx/html/assets/

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=3s --retries=3 \
  CMD wget -q -O /dev/null http://127.0.0.1:8080/ || exit 1
