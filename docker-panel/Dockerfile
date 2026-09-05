# Testowa kopia panelu WWW urzadzenia Aleksander (ESP32) — obraz Node.js.
FROM node:22-alpine

# Strefa czasowa jak na urzadzeniu (firmware liczy wszystko w czasie lokalnym Polski).
ENV TZ=Europe/Warsaw
RUN apk add --no-cache tzdata && \
    cp /usr/share/zoneinfo/Europe/Warsaw /etc/localtime && \
    echo "Europe/Warsaw" > /etc/timezone

WORKDIR /app

# Najpierw manifesty — lepszy cache warstw przy zmianach kodu.
COPY package.json package-lock.json* ./
RUN npm install --omit=dev --no-audit --no-fund

# Kod aplikacji (dane montujemy wolumenem, patrz docker-compose.yml).
COPY config.js store.js server.js ./
COPY public ./public

# Dane trzymamy w /app/data (wolumen). Gdy wolumen jest pusty, seed z obrazu wypelni brak.
COPY data ./data

ENV PORT=8080
ENV DATA_FILE=/app/data/karmienia.csv
ENV SETTINGS_FILE=/app/data/ustawienia.cfg
ENV BACKUP_FILE=/app/data/karmienia_backup.csv

EXPOSE 8080

# Prosty healthcheck: panel musi odpowiadac na /api/status.
HEALTHCHECK --interval=30s --timeout=4s --start-period=5s --retries=3 \
  CMD wget -qO- http://127.0.0.1:8080/api/status > /dev/null 2>&1 || exit 1

CMD ["node", "server.js"]
