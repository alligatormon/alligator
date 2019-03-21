FROM alpine
WORKDIR /build
ADD ./ /build/
RUN  apk add git cmake gcc g++ make libuv-dev glib-dev intltool
RUN  mkdir depends &&\
  cd depends &&\
  wget https://dl.bintray.com/alligatormon/generic/cutter-1.2.6.tar.gz &&\
  tar xvzf cutter-1.2.6.tar.gz &&\
  cd cutter-1.2.6 &&\
  ./configure --prefix=/usr && ls && make && ls && make install
RUN cd src && cmake . && make && cutter .

FROM alpine
COPY --from=0 /build/src/alligator /usr/bin/alligator
ENTRYPOINT /usr/bin/alligator
