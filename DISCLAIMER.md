# Legal Disclaimer & Compliance Notice

**Project:** AdvHD EasyModern
**Repository:** `AdvHD_mod_easymodern`
**Origin of publication:** Canada
**Intended for:** users outside China. Use in China is allowed, at your own risk.
**Language of record:** English (this version is authoritative)

---

> **This document is not legal advice.** It is a good-faith disclosure of the basis on which this
> project is published and of the expectations placed on you as a user. No lawyer–client or
> solicitor–client relationship is created by reading it or by using this project. Laws differ by
> country and change over time. If your situation is unclear, consult a qualified lawyer in your own
> jurisdiction **before** you download, build, or run anything here.

---

## 1. Who made this, and under what law

This project is contributed to by people in **multiple countries and regions**. Each contributor
contributes on the basis that their own contribution complies with the laws and policies applicable
where they are.

The project is **developed, maintained, and published from Canada**, and it is published on the basis
that it complies with applicable Canadian law, including the *Copyright Act*, R.S.C., 1985, c. C-42.
In particular:

* **Interoperability.** Canadian law provides that it is not an infringement of copyright in a
  computer program for a person who owns an authorized copy of that program — or holds a license to
  use a copy — to reproduce the copy for the sole purpose of obtaining the information needed to
  make that program and another program interoperable, subject to limits on how that information may
  be used or disclosed (*Copyright Act*, [s. 30.61](https://laws-lois.justice.gc.ca/eng/acts/C-42/section-30.61.html),
  "Interoperability of computer programs"). The reverse-engineering notes in this repository
  document the interfaces, calling conventions, and media-format constraints required to make the
  AdvHD engine interoperate with modern codecs.
* **Technological protection measures.** This project does **not** descramble, decrypt, bypass,
  remove, deactivate, or impair any technological protection measure within the meaning of
  *Copyright Act* [s. 41](https://laws-lois.justice.gc.ca/eng/acts/C-42/section-41.html). It ships no
  cracked, pre-patched, or key-generating material, and it is not intended to enable access to any
  work that you are not already entitled to access.

The summaries above are abbreviated and are provided for transparency. They are not a substitute for
the statute, and they are not a legal opinion about your particular use.

---

## 2. Use in China

This project is written and published for use outside China. It is not written for Chinese law, and
no representation is made that it complies with it.

Nothing here stops you using it in China.

* If you use it in China, including to play visual novels or galgame, that is your own call and your
  own risk. The project and its contributors accept no responsibility or liability for any
  consequence, whether under Chinese law or any other.
* We do not monitor or verify such use, and we do not support, endorse or assist it.
* Anyone who uses this project in China is responsible for their own compliance with Chinese law. The
  Canadian permissions described in §1 do not apply there.
* We do ask that you do not repost, mirror or redistribute this project, its builds or derived
  material into China. See §7 for why that is a request rather than a license term.
* If this project conflicts with the laws, religion or customs where you are, stop using it and
  remove it.

---

## 3. No game content is distributed; no affiliation

This repository contains **only original source code and documentation authored by its
contributors**. It does **not** contain, bundle, mirror, or distribute:

* any game executable, patch, or crack;
* any game asset — artwork, sprites, backgrounds, audio, video, scripts, or archives;
* any encryption or decryption keys.

This project is **not affiliated with, authorized by, endorsed by, or sponsored by** WillPlus,
RioShiina, or any other developer, publisher, or rights holder of any AdvHD Engine title. All game
titles and their assets remain the property of their respective rights holders.

---

## 4. Accepted use

This project is intended for:

* **Personal interoperability and format modernization** — enabling a game you lawfully own to play
  modern AV1/Opus video and JPEG XL textures, and to reduce storage footprint, on your own hardware;
* **Security and preservation research**, study, and teaching;
* **Interoperability study** consistent with the exception described in §1.

It is **not** intended for, and you must not use it for:

* circumventing copy protection, license checks, or digital rights management;
* distributing, selling, or publicly performing game content you do not own or license;
* creating or distributing pirated copies of any software;
* any unlawful, deceptive, or infringing purpose.

---

## 5. You must own the base game

This project is **not a game** and contains no game. It cannot run on its own. It is a set of codec
hooks and a launcher that must be placed alongside a **lawfully installed copy of the base game**,
which you must obtain and license yourself. Use it only with a copy you have lawfully acquired. Do
not use it with pirated, cracked, or otherwise unauthorized copies.

---

## 6. Free of charge — beware of impostors

This project is, and will remain, **completely free**. The contributors have never charged for it and
have never organized or participated in any paid activity in connection with it.

* **Nobody is authorized to charge you money for this project**, in any form, on any platform.
* Any website, storefront, group, channel, or individual using the `AdvHD_mod_easymodern` / AdvHD
  EasyModern name to sell access, solicit donations in exchange for the software, or gate it behind a
  paywall is **unaffiliated with this project and is likely fraudulent**.
* If you paid someone for this project, you were defrauded. **We received nothing, we owe you
  nothing, and we accept no responsibility** for that transaction.

---

## 7. Modification, redistribution, and commercial use

The **source code in this repository is licensed under the MIT License** (see [`LICENSE`](LICENSE)).
Under that license you may use, copy, modify, merge, publish, distribute, sublicense, and sell the
software, provided the copyright notice and permission notice are retained.

Alongside that license, the contributors **ask** the following of the community, and state it as
their intent:

* **Do not sell this project, and do not charge for it in any form.** See §6.
* **Do not present modified or repackaged builds as official releases**, and do not use the project
  name or contributor names to imply endorsement of a modified build.

> **An honest note on this section.** The MIT License permits modification, commercial use and
> redistribution. Those are permissions the contributors have already granted and, for the code you
> received, cannot retroactively withdraw. The requests in this document therefore express the
> contributors' intent rather than an enforceable restriction on the MIT-licensed code. Where a
> request conflicts with the MIT License, **the MIT License governs the code**.

In every case, the contributors accept **no responsibility** for any modified, repackaged, or
redistributed version they did not publish, and no responsibility for any use made of this project in
China.

---

## 8. Third-party components (obtained separately, not distributed here)

This project **dynamically loads** the following at runtime and does **not** bundle, link, or
redistribute them. Each remains governed exclusively by its own license, and **you** are responsible
for reviewing and complying with that license before obtaining or using it:

| Component | Purpose | Typical license |
| :--- | :--- | :--- |
| **LAV Filters** (`LAVSplitter.ax`, `LAVVideo.ax`, `LAVAudio.ax` and dependencies) | Registration-free DirectShow decoding for AV1/Opus | GPL-2.0-or-later |
| **libjxl** (`libjxl.dll`, `libjxl_cms.dll` and dependencies) | JPEG XL texture decoding | BSD-3-Clause |
| **HandBrakeCLI** | Offline video transcoding (SVT-AV1 / Opus) | GPL-2.0-or-later |
| **MinGW-w64 runtime** (`libgcc`, `libstdc++`) | Statically linked build runtime | GPL-3.0-or-later with GCC Runtime Library Exception |

The build scripts compile against publicly available C/C++ **headers** only. No third-party library
is statically linked into this project's outputs; every third-party symbol is resolved at runtime
through `LoadLibrary`/`GetProcAddress`. Redistributing this project together with those components
may impose additional obligations on **you** — in particular, the GPL components carry source- and
notice-availability requirements. Distributing them is your decision and your responsibility.

---

## 9. Modifying your game installation is at your own risk

This software injects code into a running process and rewrites asset containers. Such operations can
corrupt files, break saved games, destabilize the host application, trigger anti-tamper or
anti-cheat mechanisms, or violate the terms of service of a platform or storefront. Back up your
installation first. Do not use this project where doing so would breach an agreement you are party
to, or where it would disrupt a multiplayer or online service.

---

## 10. No warranty; limitation of liability

This project is licensed under the **MIT License** (see [`LICENSE`](LICENSE)). It is provided
**"AS IS"**, without warranty of any kind, express or implied, including but not limited to the
warranties of merchantability, fitness for a particular purpose, and non-infringement. In no event
shall the authors or copyright holders be liable for any claim, damages, or other liability, whether
in an action of contract, tort, or otherwise, arising from, out of, or in connection with the
software or its use.

**You assume the entire risk** of downloading, building, installing, and running this software, and
of any consequence to your game installation, your data, your hardware, or your legal standing.

---

## 11. Trademarks

All product names, game titles, company names, logos, and trademarks referenced in this repository
are the property of their respective owners and are used for identification and descriptive purposes
only. Their use does not imply any affiliation with or endorsement by the trademark owners.

---

## 12. Rights holders: contact and takedown

If you are a rights holder and you believe this repository infringes your rights, please open an
issue on this repository (or contact the maintainer through the repository's published contact
channel) with the specific material at issue and the basis for your concern. Good-faith requests
will be reviewed promptly, and material will be removed or amended where warranted. This project
distributes no game content and has no interest in hosting anything that infringes another party's
rights.

---

*Last updated: 2026-10-06*
