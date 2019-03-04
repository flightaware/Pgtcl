FROM debian:stable-slim

RUN apt-get update -qq && \
    apt install -y docbook-utils ghostscript make patch ed docbook-xsl tidy docbook5-xml

RUN mkdir /root/doc
WORKDIR /root/doc
