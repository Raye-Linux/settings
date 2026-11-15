# A system bus stand-in for test_region_system: polkit and locale1 as
# they behave on the PC.
#
# polkit asks the person and answers after ANSWER seconds, with the word
# in the file <answer> ("yes" or "no"; a new word forgets what was kept);
# a yes is kept for that program, like auth_admin_keep, so its next
# check passes at once. locale1 waits for polkit only GIVE_UP seconds,
# like localed, whose own call to polkit times out after 25 s: when the
# person takes longer, the language is never set and nobody says so.
#
#   python3 pretend_locale1.py <log> <written> <answer file> <ANSWER s> <GIVE_UP s>
import os
import sys

import dbus
import dbus.mainloop.glib
import dbus.service
from gi.repository import GLib

LOG, WRITTEN, SAY_FILE = sys.argv[1], sys.argv[2], sys.argv[3]
ANSWER, GIVE_UP = float(sys.argv[4]), float(sys.argv[5])
ACTION = "org.freedesktop.locale1.set-locale"

dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
address = os.environ["DBUS_SYSTEM_BUS_ADDRESS"]
polkit_bus = dbus.bus.BusConnection(address)
locale_bus = dbus.bus.BusConnection(address)


def log(*words):
    with open(LOG, "a") as f:
        f.write(" ".join(str(w) for w in words) + "\n")


def say():
    with open(SAY_FILE) as f:
        return f.read().strip()


class Authority(dbus.service.Object):
    kept = set()
    said = None

    @dbus.service.method("org.freedesktop.PolicyKit1.Authority",
                         in_signature="(sa{sv})sa{ss}us", out_signature="(bba{ss})",
                         async_callbacks=("ok", "err"))
    def CheckAuthorization(self, subject, action, details, flags, cancel, ok, err):
        word = say()
        if word != self.said:
            self.kept.clear()
            self.said = word
        who = (str(subject[1].get("name", "")), str(action))
        if who in self.kept:
            log("kept", action)
            ok((True, False, {}))
            return
        if not flags & 1:
            ok((False, True, {}))
            return
        log("prompt", action)

        def answer():
            yes = word == "yes"
            if yes:
                self.kept.add(who)
            log("answered", word)
            ok((yes, False, {}))
            return False

        GLib.timeout_add(int(ANSWER * 1000), answer)


class Locale(dbus.service.Object):
    @dbus.service.method("org.freedesktop.locale1", in_signature="asb", out_signature="",
                         sender_keyword="sender", async_callbacks=("ok", "err"))
    def SetLocale(self, entries, interactive, sender, ok, err):
        log("SetLocale", ",".join(entries))
        subject = ("system-bus-name", {"name": dbus.String(sender, variant_level=1)})

        def checked(result):
            if not result[0]:
                err(dbus.exceptions.DBusException("Access denied",
                                                  name="org.freedesktop.DBus.Error.AccessDenied"))
                return
            with open(WRITTEN, "w") as f:
                f.write("\n".join(entries) + "\n")
            log("written")
            ok()

        def failed(e):
            log("gave up", e.get_dbus_name())
            err(dbus.exceptions.DBusException("Method call timed out",
                                              name="org.freedesktop.DBus.Error.NoReply"))

        polkit_bus.call_async("org.freedesktop.PolicyKit1", "/org/freedesktop/PolicyKit1/Authority",
                              "org.freedesktop.PolicyKit1.Authority", "CheckAuthorization",
                              "(sa{sv})sa{ss}us",
                              (subject, ACTION, {}, dbus.UInt32(1 if interactive else 0), ""),
                              checked, failed, timeout=GIVE_UP)


Authority(polkit_bus, "/org/freedesktop/PolicyKit1/Authority")
Locale(locale_bus, "/org/freedesktop/locale1")
names = [dbus.service.BusName("org.freedesktop.PolicyKit1", polkit_bus),
         dbus.service.BusName("org.freedesktop.locale1", locale_bus)]
GLib.MainLoop().run()
