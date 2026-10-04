/**
 * Settings field registry — shared keys for USB read and CLI apply.
 */
(function (App) {
  "use strict";

  App.SETTINGS_FIELDS = [
    { id: "setting-repeat", readCmd: "get repeat", cliKey: "repeat", tier: "general" },
    { id: "setting-owner-info", readCmd: "get owner.info", cliKey: "owner.info", tier: "general" },
    { id: "setting-guest-password", readCmd: "get guest.password", cliKey: "guest.password", tier: "general" },
    { id: "setting-dutycycle", readCmd: "get dutycycle", cliKey: "dutycycle", tier: "general" },
    { id: "setting-flood-advert-interval", readCmd: "get flood.advert.interval", cliKey: "flood.advert.interval", tier: "advanced" },
    { id: "setting-advert-interval", readCmd: "get advert.interval", cliKey: "advert.interval", tier: "advanced" },
    { id: "setting-flood-max-unscoped", readCmd: "get flood.max.unscoped", cliKey: "flood.max.unscoped", tier: "advanced" },
    { id: "setting-flood-max-advert", readCmd: "get flood.max.advert", cliKey: "flood.max.advert", tier: "advanced" },
    { id: "setting-flood-max", readCmd: "get flood.max", cliKey: "flood.max", tier: "advanced" },
    { id: "setting-path-hash-mode", readCmd: "get path.hash.mode", cliKey: "path.hash.mode", tier: "advanced" },
    { id: "setting-loop-detect", readCmd: "get loop.detect", cliKey: "loop.detect", tier: "advanced" },
    { id: "setting-txdelay", readCmd: "get txdelay", cliKey: "txdelay", tier: "expert" },
    { id: "setting-direct-txdelay", readCmd: "get direct.txdelay", cliKey: "direct.txdelay", tier: "expert" },
    { id: "setting-rxdelay", readCmd: "get rxdelay", cliKey: "rxdelay", tier: "expert" },
    { id: "setting-radio-rxgain", readCmd: "get radio.rxgain", cliKey: "radio.rxgain", tier: "expert" },
    { id: "setting-int-thresh", readCmd: "get int.thresh", cliKey: "int.thresh", tier: "expert" },
    { id: "setting-agc-reset", readCmd: "get agc.reset.interval", cliKey: "agc.reset.interval", tier: "expert" },
    { id: "setting-multi-acks", readCmd: "get multi.acks", cliKey: "multi.acks", tier: "expert" },
    { id: "setting-lat", readCmd: "get lat", cliKey: "lat", tier: "general" },
    { id: "setting-lon", readCmd: "get lon", cliKey: "lon", tier: "general" },
    { id: "setting-advert-loc", readCmd: "gps advert", cliKey: "gps advert", tier: "general" },
  ];
})(window.ConfiguratorApp);
