<p align="right">
  <a href="fork-guide.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Fork Workflow

This repository keeps `main` as the current public Vokie AI Passport firmware
baseline. Fork-specific product work belongs on `feature/*` branches so a fork
can synchronize its `main` without mixing local changes into the public
baseline.

## Repository roles

```text
README.md              fork product overview and integration requirements
docs/                  protocol, hardware, engineering, and contribution docs
components/bsp/        stable board APIs and hardware implementation
main/                  Vokie BLE peripheral, audio transport, and status UI
assets/                reusable source assets
skills/                reusable AI-agent skills
tests/                 host-runnable tests
sdkconfig.defaults     reproducible ESP32-C3 defaults
```

## Recommended workflow

1. Fork `alanshen6666-collab/vokie-ai-passport-firmware`.
2. Keep the fork's `main` synchronized with the public repository's `main`.
3. Create each change from the latest `main` on a short-lived `feature/*` or
   `fix/*` branch.
4. Run `./tools/validate.sh` with ESP-IDF 5.5.3 before opening a pull request.
5. Merge through a reviewed pull request instead of developing directly on
   `main`.

This standalone repository does not automatically synchronize with FoloToy.
To update a downstream fork, fetch this repository explicitly, review the diff,
and merge the public `main` through the fork's normal pull-request workflow.

## Upstream attribution

This firmware is derived from
[FoloToy/ai-passport](https://github.com/FoloToy/ai-passport) and preserves its
MIT license and Git history. Reusable changes to the hardware baseline may be
proposed to FoloToy separately; Vokie-specific firmware, protocol, UI, and brand
assets belong in this repository.

## Documentation and assets

Keep English at each default `.md` path and Simplified Chinese in a paired
`.zh_CN.md` file with reciprocal language links. Put product-specific design
notes under `docs/` and reusable binary/source assets under `assets/`.

The Vokie name and symbol are excluded from MIT. Forks may redistribute the
unmodified symbol only under
[`LICENSES/Vokie-Brand-Asset.txt`](../LICENSES/Vokie-Brand-Asset.txt); replacing
or extracting it for another brand requires separate permission.
