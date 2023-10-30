#!/bin/echo run with: docker build . -f
# -*- coding: utf-8 -*-

FROM i386/debian:stretch
LABEL maintainer="Philippe Coval <philippe.coval@silabs.com>"

ENV DEBIAN_FRONTEND noninteractive

RUN echo "# log: Setup system"  \
  && set -x \
  && sed -e 's|\(http://\)\(.*\)\(.debian.org\)|\1archive\3|g' -i /etc/apt/sources.list \
  && sed -e 's|stretch-updates|stretch-proposed-updates|g' -i /etc/apt/sources.list \
  && apt-get update -y \
  && apt-get install -y sudo make \
  && date -u

ENV project zw-zgw
ENV workdir /usr/local/src/${project}
WORKDIR ${workdir}
COPY helper.mk ${workdir}
RUN echo "# log: Install ${project}" \
  && set -x  \
  && ./helper.mk setup/debian/stretch \
  && date -u

COPY . ${workdir}
WORKDIR ${workdir}
RUN echo "# log: Build ${project}" \
  && set -x  \
  && ./helper.mk \
  && date -u

ENTRYPOINT [ "${workdir}/helper.mk" ]
CMD [ "start", "args=--help"]
