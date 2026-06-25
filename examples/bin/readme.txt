demo.app/ is preserved for the future option of shipping fury inside a
macOS .app bundle (icon + Info.plist scaffold). It is NOT used by the
current build:

  Default workflow (since the editor refactor): the engine resolves
  Resource/ from the current working directory. Run `./fury Editor.lua`
  from the examples/ folder. No copying required.

  CFBundle workflow (legacy): if you uncomment the CFBundle branch in
  engine/Fury/FileUtil.cpp and bundle fury into demo.app, copy the
  Resource/ tree to demo.app/Contents/Resources/ so CFBundle can find it.
