// SPDX-License-Identifier: GPL-3.0-or-later
// The words for what Raye Update could not read, in one place for the three
// places that say it. `notice` comes from updatetext.js sourceNotice().
import QtQml

QtObject {
    property string notice

    readonly property string text: {
        switch (notice) {
        case "raye": return i18nc("@info", "Raye updates couldn't be checked.")
        case "offline": return i18nc("@info", "One place updates come from couldn't be reached. The rest were checked.")
        case "missing": return i18nc("@info", "One place updates come from has nothing to offer right now. The rest were checked.")
        case "unsigned": return i18nc("@info", "One place updates come from couldn't be trusted, so it was skipped.")
        case "invalid": return i18nc("@info", "One place updates come from sent something unusable. The rest were checked.")
        case "unknown": return i18nc("@info", "One place updates come from couldn't be read. The rest were checked.")
        default: return ""
        }
    }
}
