# Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.
FROM node:10

# configure for Epic's p4 server
ENV P4PORT=perforce:1666

# set this to the robomerge account
ENV P4USER=robomerge

# BOTNAME and P4PASSWD should be set on the run (bot defaults to test just for safety)
ENV BOTNAME=test
ENV P4PASSWD=

# set the APP directory as . in the image
WORKDIR /app

# make the directory to sync branch settings to
RUN mkdir /app/data

# make the root directory for workspaces
RUN mkdir /src

# make an empty settings folder in able to run without error
RUN mkdir -p /root/.robomerge

# expose the robomerge web page
EXPOSE 8080 4433

# install typescript
RUN npm install -g typescript@^3.0

# install p4 command line
COPY ./bin/p4 /usr/local/bin/p4

# install dependencies
COPY ./package.json ./
COPY ./package-lock.json ./
RUN npm install

# copy web resources
COPY ./public ./public
COPY ./certs ./certs

# copy and compile code
COPY ./tsconfig.json ./
COPY ./src ./src
RUN tsc

# copy configuration
COPY ./version.json ./
COPY ./config ./config

CMD [ "node", "dist/robo/watchdog.js" ]
