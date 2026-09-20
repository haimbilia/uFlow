# Match WiiFlow v5.6.2's upstream CI SDK, separate from the Wii U SDK.
FROM uflow-app-builder:dev
RUN apt-get update && apt-get install -y --no-install-recommends zstd
RUN mkdir -p /opt/wiiflow-sdk /tmp/wiiflow-sdk && cd /tmp/wiiflow-sdk && \
    for pkg in devkitPPC-r42.2-1-linux_x86_64.pkg.tar.zst devkitppc-rules-1.1.1-1-any.pkg.tar.xz libogc-2.4.0-1-any.pkg.tar.zst general-tools-1.2.0-2-linux_x86_64.pkg.tar.xz gamecube-tools-1.0.3-1-linux_x86_64.pkg.tar.xz; do \
      curl -fsSL --retry 2 "https://wii.leseratte10.de/devkitPro/file.php/$pkg" -o "$pkg" && \
      tar -xf "$pkg" --strip-components=1 -C /opt/wiiflow-sdk || exit 1; \
    done
ENV DEVKITPRO=/opt/wiiflow-sdk/devkitpro
ENV DEVKITPPC=/opt/wiiflow-sdk/devkitpro/devkitPPC
ENV PATH=/opt/wiiflow-sdk/devkitpro/devkitPPC/bin:/opt/wiiflow-sdk/devkitpro/tools/bin:${PATH}
WORKDIR /wiiflow
