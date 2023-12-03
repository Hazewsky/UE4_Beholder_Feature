# Use an official image
FROM centos7_base

# Set the working directory
WORKDIR /home/buildmaster

# Copy needed files into the container
COPY --chown=buildmaster:buildmaster *.sh /home/buildmaster/
COPY --chown=buildmaster:buildmaster *.xz /home/buildmaster/

# Run the build
CMD su buildmaster -c "/bin/bash build_python_linux.sh"
#CMD /bin/bash
