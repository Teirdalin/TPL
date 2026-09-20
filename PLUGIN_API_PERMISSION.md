# TPL Plugin-Development Permission

Copyright (c) 2026 Teirdalin.

This is Teirdalin's separate written permission under JDL-1 section 5. It is
specific to the TPL API and example identified below; it is not a requirement
that plugin creators adopt JDL-1 for their own code.

You may create, use, modify, distribute, sublicense, and sell your own
independently authored plugins that interact with TPL's public plugin
interface, under terms of your choice. Plugins may be free, commercial,
open-source, or closed-source. TPL imposes no requirement to publish, upload,
or provide plugin source code, and no royalty is owed to Teirdalin merely
for developing or distributing a plugin.

For plugin development, Teirdalin permits copying, adapting, compiling, and
distributing `include/tpl.h`, `include/tpllib.h`, `include/tpllib_ui.h`,
`include/tpllib_engine.h`, `include/tpllib_engine_save.hpp`,
the generated SDK header `include/tpllib_engine_generated.hpp`,
`tests/sample.cpp`, `examples/tpllib-diagnostics.cpp`, and `examples/save-menu.cpp`,
including modified forms,
within plugins and plugin-development kits. Retain their copyright notices
and copies of this permission and `Licenses/JDL-1.txt` in accompanying source,
documentation, or legal notices. Mark modifications to those files. Every
recipient has this same permission for those API/example portions.

This permission does not place your independently authored plugin code under
JDL-1 and does not grant others permission to modify or redistribute your
plugin. Those permissions are controlled by your chosen terms. If you opt
into JDL-1 for your plugin, you are its Creator; permission comes from you or
the appropriate rights holder, not from Teirdalin as the loader author.

This grant does not extend to TPL's other implementation files or loader
binaries, including TPLLib's runtime implementation and static library.
Calling the TPLLib API provided by TPL does not require incorporating that
implementation into your plugin. JDL-1 section 3 permits personal-use modifications to
those files. Redistribution or bundling, and modifications for other purposes,
require Teirdalin's separate written permission. Third-party dependencies
retain their own requirements.
