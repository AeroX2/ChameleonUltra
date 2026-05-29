![logo](docs/images/ultra-logo.png)

![ultra picture](docs/images/ultra-overview.png)

# ChameleonUltra Authorized Distributors

Lyon, France: [Lab401](https://lab401.com/)

Santa Ana, United States: [Hackerwarehouse](https://hackerwarehouse.com/)

Hastings, UK: [KSEC](https://labs.ksec.co.uk/product/proxgrind-chameleon-ultra/)

Montreal, Canada: [TechSecurityTools](https://techsecuritytools.com/product/chameleon-ultra/)

Shenzhen, China: [Sneaktechnology](https://sneaktechnology.com)

Guangdong, China: [MTools Tec](https://shop.mtoolstec.com/)

Lazada One, Singapore: [Aliexpress by RRG](https://proxgrind.aliexpress.com/store/1101312023)

# What is it and how to use ?

Read the [available documentation](https://github.com/RfidResearchGroup/ChameleonUltra/wiki).

# Custom firmware features (this fork)

This fork adds a set of on-device usability and performance improvements on top
of the upstream firmware. All of the following are merged into `main`.

### Buttons & on-device controls
* **Long-press fires while held** — a long-press action triggers the instant its
  hold threshold is reached (with LED feedback), instead of waiting for release.
* **Double-click bindings** — a quick double-tap can be mapped to its own action,
  doubling the functions reachable from the two buttons.
* **A+B chord binding** — pressing both buttons together triggers a dedicated
  action without firing the individual button events.
* **WRITE button** — write the active slot to a physical card, guarded by an
  A, B, A+B confirmation sequence to prevent accidental overwrites.
* **FULLREAD button** — on-device dump of a card using the built-in default-key
  dictionary, no host/CLI required.
* **Boot-time slot select** — hold a button during power-on to jump straight to a
  chosen slot.

### Cloning
* **CLONE wait + scratch slot** — the on-device clone waits ~5 s for the card to
  be presented and writes the capture into a dedicated scratch slot, so an
  existing slot is never clobbered.

### BLE radio
* **Persisted on/off toggle (A+B chord)** — turn the BLE radio on or off from the
  device; the choice survives reboots.
* **LED feedback on toggle** — a blue blink for ON, red for OFF, so the state
  change is visible.
* **True disconnect on OFF** — disabling the radio also drops any active
  connection, not just future advertising.
* **Skip BLE init at boot when disabled** — when the radio is left off, the BLE
  stack is not brought up at boot, for a faster and lower-power start.

### Responsiveness & performance
* **Non-blocking boot/wake LED animation** — the device starts emulating and
  answering the host almost immediately; the LED sweep now plays in parallel
  instead of stalling startup.
* **Snappier slot switching** — the slot-change LED fade no longer blocks the
  device for ~250 ms per switch.
* **Lower input latency** — button debounce reduced (50 ms → 25 ms) and the
  double-click detection window shortened (250 ms → 200 ms).

# Compatible applications

* [ChameleonUltraGUI](https://github.com/GameTec-live/ChameleonUltraGUI)
* [MTools BLE](https://github.com/RfidResearchGroup/ChameleonUltra/wiki/mtoolsble)
* [Mifare Chameleon Tool (iOS only, Beta)](https://apps.apple.com/it/app/mifare-chameleon-tool/id6761231484)
* [Chameleon Ultra (Sailfish OS only)](https://sailfishos-chum.github.io/apps/harbour-chameleon-ultra)

# Videos

*Beware some of the instructions might have changed since recording, check the current documentation when in doubt!*

* [Downloading and compiling the official CLI](https://www.youtube.com/watch?v=VGpAeitNXH0)
* [Downloading ChameleonUltraGUI](https://www.youtube.com/watch?v=rHH7iqbX3nY)
* [ChameleonUltraGUI features overview](https://www.youtube.com/watch?v=YqE8wyVSse4)
* [Using ChameleonUltraGUI and the Chameleon Ultra](https://www.youtube.com/watch?v=9jtKNJ5-kVY)
* [MTools BLE - How to clone a card with ChameleonUltra](https://youtu.be/IvH-xtdW1Wk?si=4exqgAAeJ-kxU3aN)

# Official channels

Where do you find the community?
* [RFID Hacking community discord server](https://t.ly/d4_C)
  * Software/chameleon-dev for firmware and clients development discussions
  * Devices/chameleon-ultra for usage discussions
* [GameTec_live discord server](https://discord.gg/DJ2A4wxncK)

###### Searching for the docs repo? Find it [here](https://github.com/RfidResearchGroup/ChameleonUltraDocs)
