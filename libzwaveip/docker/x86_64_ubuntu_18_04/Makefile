DOCKER_NAME=zwave/libzwaveip_$(shell basename $(CURDIR))

all: image push

image:
	docker build -t ${DOCKER_NAME} .

push:
	docker push ${DOCKER_NAME}

.PHONY: all image push
