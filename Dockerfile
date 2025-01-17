FROM ubuntu:18.04

RUN mkdir -p /app
WORKDIR /app

RUN apt-get update
RUN apt-get install -y clustalo ncbi-tools-bin
RUN apt-get install -y make rsync wget
RUN apt-get install -y git g++ libz-dev libbz2-dev bzip2 doxygen xsltproc docbook docbook-xsl docbook-xml autoconf automake autotools-dev liblog4cpp5-dev libperl-dev
RUN mkdir -p /deps

# Install boost 1.65
WORKDIR /deps/
RUN wget https://dl.bintray.com/boostorg/release/1.65.1/source/boost_1_65_1.tar.bz2 && tar xjf boost_1_65_1.tar.bz2 && rm boost_1_65_1.tar.bz2
WORKDIR /deps/boost_1_65_1
RUN ./bootstrap.sh && ./b2 install && ldconfig
RUN install bjam /usr/local/bin/bjam
ENV BOOST_ROOT /deps/boost_1_65_1

# Install libzeep
RUN git clone https://github.com/mhekkel/libzeep.git /deps/libzeep ;\
    cd /deps/libzeep ;\
    git checkout tags/v3.0.3
# XXX: Workaround due to bug in libzeep's makefile
RUN sed -i '71s/.*/\t\$\(CXX\) \-shared \-o \$@ \-Wl,\-soname=\$\(SO_NAME\) \$\(OBJECTS\) \$\(LDFLAGS\)/' /deps/libzeep/makefile
WORKDIR /deps/libzeep
# XXX: Run ldconfig manually to work around a bug in libzeep's makefile
RUN make -j ; make install ; ldconfig

WORKDIR /app
COPY . /app
RUN ./configure && make -j && make install
RUN mkdir -p /srv/files && cp -r /srv/mrs-data/* /srv/files

# The config file generated by the makefile is never correct.
# Previously the generated config file has been edited by hand to set the
# correct <base-url> address.
#
# The problem is that MRS_BASE_URL needs to be set to the hostname and port
# machine running the docker container for the web service (e.g.
# chelonium:18090) but to the externally visible hostname and port for the SOAP
# service (because this is set in the WSDL).
#
# We can workaround this by replacing the <base-url> value in the generated
# config.
RUN sed -i '30s/http\:\/\/chelonium\.cmbi\.umcn\.nl\:18090/https\:\/\/mrs.cmbi.umcn.nl/' /usr/local/etc/mrs/mrs-config.xml


EXPOSE 18090
