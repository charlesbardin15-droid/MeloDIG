// Carlito Audio : essai de 14 jours + activation par clé (commun aux 4 plugins).
//
// Une clé = signature RSA-1024 de "CARLITO-v1|<produit>|<email>" faite par carlito_keygen.py.
// Le plugin ne contient que la clé publique : il peut vérifier une clé, pas en fabriquer.
// Une clé PACK débloque les 4 plugins.
#pragma once

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <atomic>

namespace carlito
{
class Licence
{
public:
    static constexpr int trialDays = 14;
    enum class State { trial, expired, licensed };

    // productId : "CFX" CarliFX, "CTU" Carlitune, "MDG" MeloDIG, "CTP" CarliTpitch
    explicit Licence (juce::String productId, juce::File folder = {})
        : product (std::move (productId)),
          dir (folder != juce::File() ? folder : defaultFolder())
    {
        refresh();
    }

    // Relit les fichiers et recalcule l'état. Thread du message (ou constructeur).
    void refresh()
    {
        const juce::ScopedLock sl (lock);
        dir.createDirectory();
        if (loadKey()) { setState (State::licensed, 0); return; }

        const juce::int64 now = juce::Time::currentTimeMillis();
        juce::int64 start = 0, seen = 0;
        bool tampered = false, found = false;
        for (auto& f : trialFiles())
        {
            juce::int64 s = 0, l = 0;
            const int r = readTrial (f, s, l);
            if (r < 0) tampered = true;
            if (r > 0) { start = found ? juce::jmin (start, s) : s; seen = juce::jmax (seen, l); found = true; }
        }
        if (! found && tampered) { setState (State::expired, 0); return; }   // toutes les copies modifiées
        if (! found) start = seen = now;
        const bool clockBack = now < seen - dayMs * 2;
        seen = juce::jmax (seen, now);
        for (auto& f : trialFiles()) writeTrial (f, start, seen);   // répare une copie effacée ou abîmée

        const double daysUsed = (double) (now - start) / (double) dayMs;
        const int left = juce::jlimit (0, trialDays, (int) std::ceil ((double) trialDays - daysUsed));
        if (clockBack || left <= 0) setState (State::expired, 0);
        else setState (State::trial, left);
    }

    // Vérifie la clé ; si elle est bonne, l'enregistre et passe en "licensed".
    bool activate (const juce::String& email, const juce::String& key)
    {
        const auto mail = email.trim().toLowerCase();
        if (! isValid (product, mail, key) && ! isValid ("PACK", mail, key)) return false;
        const juce::ScopedLock sl (lock);
        dir.createDirectory();
        keyFile().replaceWithText (mail + "\n" + cleanKey (key) + "\n");
        if (product != "PACK" && isValid ("PACK", mail, key))   // une clé pack sert aux 4 plugins
            dir.getChildFile ("PACK.key").replaceWithText (mail + "\n" + cleanKey (key) + "\n");
        owner = mail;
        setState (State::licensed, 0);
        return true;
    }

    State getState() const noexcept { return (State) state.load(); }
    int getDaysLeft() const noexcept { return daysLeft.load(); }
    juce::String getOwner() const { const juce::ScopedLock sl (lock); return owner; }
    const juce::String& getProduct() const noexcept { return product; }

    // Lu par le thread audio : faux une fois l'essai terminé.
    bool audioAllowed() const noexcept { return getState() != State::expired; }

    static bool isValid (const juce::String& productId, const juce::String& email, const juce::String& key)
    {
        juce::MemoryBlock raw;
        if (! base32Decode (cleanKey (key), raw) || raw.getSize() != 128) return false;
        juce::BigInteger s, n, m;
        s.parseString (juce::String::toHexString (raw.getData(), (int) raw.getSize(), 0), 16);
        n.parseString (publicModulusHex, 16);
        if (s.isZero() || s >= n) return false;
        const auto text = "CARLITO-v1|" + productId + "|" + email.trim().toLowerCase();
        m.parseString (juce::SHA256 (text.toUTF8()).toHexString(), 16);
        s.exponentModulo (juce::BigInteger (65537), n);
        return s == m;
    }

    static juce::File defaultFolder()
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Application Support/Carlito Audio");
       #else
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Carlito Audio");
       #endif
    }

private:
    static constexpr juce::int64 dayMs = 24LL * 3600 * 1000;
    static constexpr const char* publicModulusHex =
        "a959c435b969e20b3426f37768ed701d428af29ccef1bedd83469e94b945be2d9041e4746d219c682c4ea9fde8115538"
        "d345dfb287290329a8a6020e373948a907eaa6a06c4909371379ce91165a11afa975db63084e0bccfa4e38b242f7ddc7"
        "a8850f3dfd3a1d18c0de712ad5274aa10617c50974a03f113102605abbe108f5";
    static constexpr const char* seal = "carlito/frpt/9c41e7";

    juce::File keyFile() const { return dir.getChildFile (product + ".key"); }

    bool loadKey()
    {
        for (auto f : { keyFile(), dir.getChildFile ("PACK.key") })
        {
            juce::StringArray lines;
            lines.addLines (f.loadFileAsString());
            if (lines.size() >= 2
                && (isValid (product, lines[0], lines[1]) || isValid ("PACK", lines[0], lines[1])))
            {
                owner = lines[0].trim();
                return true;
            }
        }
        return false;
    }

    // Deux copies de la date de début, scellées : effacer l'une ne remet pas l'essai à zéro.
    juce::Array<juce::File> trialFiles() const
    {
        juce::Array<juce::File> a { dir.getChildFile ("." + product + ".trial") };
       #if JUCE_MAC
        a.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Caches/com.carlito.audio").getChildFile ("." + product));
       #else
        a.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile (".carlito-audio").getChildFile ("." + product));
       #endif
        return a;
    }

    juce::String sealOf (juce::int64 start, juce::int64 seen) const
    {
        return juce::SHA256 ((juce::String (seal) + product + juce::String (start) + "|" + juce::String (seen)).toUTF8())
                   .toHexString().substring (0, 24);
    }

    // 1 = lu, 0 = absent, -1 = modifié à la main
    int readTrial (const juce::File& f, juce::int64& start, juce::int64& seen) const
    {
        if (! f.existsAsFile()) return 0;
        auto parts = juce::StringArray::fromTokens (f.loadFileAsString().trim(), "|", "");
        if (parts.size() != 3) return -1;
        start = parts[0].getLargeIntValue(); seen = parts[1].getLargeIntValue();
        return parts[2] == sealOf (start, seen) ? 1 : -1;
    }

    void writeTrial (const juce::File& f, juce::int64 start, juce::int64 seen) const
    {
        f.getParentDirectory().createDirectory();
        f.replaceWithText (juce::String (start) + "|" + juce::String (seen) + "|" + sealOf (start, seen));
    }

    void setState (State s, int left) { daysLeft.store (left); state.store ((int) s); }

    static juce::String cleanKey (const juce::String& k)
    {
        return k.toUpperCase().retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567");
    }

    static bool base32Decode (const juce::String& s, juce::MemoryBlock& out)
    {
        static const juce::String alphabet ("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567");
        out.reset();
        juce::uint32 buffer = 0; int bits = 0;
        for (auto c : s)
        {
            const int v = alphabet.indexOfChar (c);
            if (v < 0) return false;
            buffer = (buffer << 5) | (juce::uint32) v; bits += 5;
            if (bits >= 8) { bits -= 8; const auto b = (juce::uint8) ((buffer >> bits) & 0xff); out.append (&b, 1); }
        }
        return true;
    }

    const juce::String product;
    const juce::File dir;
    juce::CriticalSection lock;
    juce::String owner;
    std::atomic<int> state { (int) State::trial }, daysLeft { trialDays };

    JUCE_DECLARE_NON_COPYABLE (Licence)
};
}
