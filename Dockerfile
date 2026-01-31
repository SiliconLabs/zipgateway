#!/bin/echo run with: docker build . -f
# -*- coding: utf-8 -*-

FROM arm32v7/debian:stretch AS base
ENV target_debian_arch=armhf

LABEL maintainer="Laudin Molina Troconis <laudin.molinatroconis@silabs.com>"

ENV DEBIAN_FRONTEND=noninteractive

RUN echo "# log: Setup system"  \
  && set -x \
  && sed -e 's|\(http://\)\(.*\)\(.debian.org\)|\1archive\3|g' -i /etc/apt/sources.list \
  && sed -e 's|stretch-updates|stretch-proposed-updates|g' -i /etc/apt/sources.list \
  && apt-get update -y \
  && apt-get install -y sudo make \
  && date -u

FROM base AS dev
ENV project=zipgateway
ENV workdir=/usr/local/src/${project}
WORKDIR ${workdir}
COPY helper.mk ${workdir}
RUN echo "# log: Setup dependencies for ${project}" \
  && set -x  \
  && ./helper.mk setup/debian/stretch \
  && apt-get autoremove --purge \
  && apt-get clean \
  && date -u

FROM dev AS runtime
COPY . ${workdir}
RUN echo "# log: Build ${project}" \
  && set -x  \
  && ./helper.mk \
  && date -u

ENTRYPOINT [ "${workdir}/helper.mk" ]
CMD [ "start", "args=--help"]
