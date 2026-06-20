{
  lib,
  stdenv,
  meson,
  ninja,
  pkg-config,
  wrapGAppsHook4,
  gtk4,
  libadwaita,
  tesseract,
  wl-clipboard,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "waytator";
  version = "1.3.1";

  src = ../.;

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    wrapGAppsHook4
  ];

  buildInputs = [
    gtk4
    libadwaita
  ];

  runtimeInputs = [
    tesseract
    wl-clipboard
  ];

  meta = with lib; {
    description = "Screenshot annotator and lightweight image editor";
    homepage = "https://github.com/ItsLemmy/waytator";
    license = licenses.gpl3Plus;
    platforms = platforms.linux;
    mainProgram = "waytator";
  };
})
