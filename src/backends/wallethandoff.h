// SPDX-License-Identifier: GPL-3.0-or-later
// The saved passwords (KDE wallet) across a switch from Settings >
// Personalization > Desktop environment.
//
// A normal sign-in unlocks the wallet with the sign-in password
// (pam_kwallet: the key is PBKDF2-SHA512 of the password and the wallet's
// own salt, ~/.local/share/kwalletd/kdewallet.salt). The switch signs the
// person in once without a password, so nothing could unlock it, and the
// first app that wanted a saved password opened KDE's raw "KDE has
// requested to open the wallet" window (owner's PC, Beta 2).
//
// Now the page asks for the password inline before switching, only when
// such a wallet exists. Settings checks it the way the lock screen does
// (unix_chkpwd: the caller's own password only), turns it into the same
// key pam_kwallet makes, and gives the key — never the password — to a
// small keeper (raye-settings --wallet-keep, a user service outside the
// session, so it outlives the sign-out): it holds the key in memory only,
// behind a socket in the person's private runtime folder, for at most
// three minutes, and hands it out once. The new desktop's restore step
// (raye-settings --wallet-open) takes it and gives it to the wallet
// service (org.kde.KWallet.pamOpen, the call pam_kwallet's own key goes
// to), which opens the wallet as a normal sign-in would. Nothing is
// written to disk; the wallet's encryption is not changed.
#pragma once

#include <QByteArray>
#include <QString>

#include <functional>

namespace WalletHandoff
{
// the folder of the wallets (~/.local/share/kwalletd)
QString walletFolder();
// a wallet the sign-in password opens: kdewallet.kwl with its salt
bool loginWalletExists(const QString &folder = walletFolder());
// pam_kwallet's key: PBKDF2-SHA512(password, salt, 50000 rounds), 56 bytes
QByteArray deriveKey(const QByteArray &password, const QByteArray &salt);
// the salt of kdewallet, empty when there is none or it is not 56 bytes
QByteArray readSalt(const QString &folder = walletFolder());
// the keeper's socket, in the person's private runtime folder
QString socketPath();

// the person's own sign-in password? (unix_chkpwd, as the lock screen)
// done(true) when it is; the program can be changed by the tests
void checkPassword(const QString &login, const QString &password, std::function<void(bool)> done,
                   const QString &chkpwd = QStringLiteral("/usr/sbin/unix_chkpwd"));
// start the keeper and give it the key; done(ok)
void handOver(const QByteArray &key, std::function<void(bool)> done);

// raye-settings --wallet-keep: hold one key, give it once, at most 180 s
int keep(int timeoutMs = 180000);
// raye-settings --wallet-open: take the key, open the wallet with it
int open();
}
