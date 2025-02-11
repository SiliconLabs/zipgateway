#!/bin/echo run with: docker build . -f
# -*- coding: utf-8 -*-

FROM arm32v7/debian:buster
ENV target_debian_arch armhf

LABEL maintainer="Philippe Coval <philippe.coval@silabs.com>"

ENV DEBIAN_FRONTEND noninteractive

RUN echo "# log: Setup system"  \
  && set -x \
  && apt-get update -y \
  && apt-get install -y sudo make \
  && date -u

ENV project zipgateway
ENV workdir /usr/local/src/${project}
WORKDIR ${workdir}
COPY helper.mk ${workdir}
RUN echo "# log: Install ${project}" \
  && set -x  \
  && ./helper.mk setup/debian/buster\
  && date -u

COPY . ${workdir}
WORKDIR ${workdir}
RUN echo "# log: Build ${project}" \
  && set -x  \
  && ./helper.mk \
  && date -u

ENTRYPOINT [ "${workdir}/helper.mk" ]
CMD [ "start", "args=--help"]
