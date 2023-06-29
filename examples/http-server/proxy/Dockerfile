FROM alpine:3.17

RUN mkdir /opt/app
WORKDIR /opt/app

RUN apk update && \
    apk add nodejs npm && \
    npm install dd-trace

COPY ./proxy.js /opt/app/proxy.js

CMD ["node", "--require", "dd-trace/init", "proxy.js"]
