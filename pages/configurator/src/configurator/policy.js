/**
 * Policy checkbox mutual exclusion and bulk-action helpers.
 */
(function (App) {
  "use strict";

  App.syncScopeMastersForSubsection = function (subsection) {
    if (!subsection || typeof syncScopeMasters === "function") return;
  };

  App.setPolicyCheckboxes = function (policyCard, selector, checked, respectDisabled) {
    if (!policyCard) return;
    policyCard.querySelectorAll(selector).forEach(function (el) {
      if (respectDisabled && el.disabled) return;
      el.checked = checked;
    });
  };

  App.toggleAllowDenyPair = function (policyCard, target) {
    if (!policyCard || !(target instanceof HTMLInputElement)) return;
    const code = target.getAttribute("data-code");
    if (!code) return;
    if (target.classList.contains("policy-allow") && target.checked) {
      const d = policyCard.querySelector(
        'input.policy-deny[data-code="' + code + '"]',
      );
      if (d && !d.disabled) d.checked = false;
    } else if (target.classList.contains("policy-deny") && target.checked) {
      const a = policyCard.querySelector(
        'input.policy-allow[data-code="' + code + '"]',
      );
      if (a && !a.disabled) a.checked = false;
    }
  };

  App.applyScopeMaster = function (subsection, mode, checked) {
    if (!subsection) return;
    const allowSel = "input.policy-allow";
    const denySel = "input.policy-deny";
    if (mode === "allow") {
      if (checked) {
        subsection.querySelectorAll(denySel).forEach(function (el) {
          if (!el.disabled) el.checked = false;
        });
        subsection.querySelectorAll(allowSel).forEach(function (el) {
          if (!el.disabled) el.checked = true;
        });
      } else {
        subsection.querySelectorAll(allowSel).forEach(function (el) {
          if (!el.disabled) el.checked = false;
        });
      }
    } else if (mode === "deny") {
      if (checked) {
        subsection.querySelectorAll(allowSel).forEach(function (el) {
          if (!el.disabled) el.checked = false;
        });
        subsection.querySelectorAll(denySel).forEach(function (el) {
          if (!el.disabled) el.checked = true;
        });
      } else {
        subsection.querySelectorAll(denySel).forEach(function (el) {
          if (!el.disabled) el.checked = false;
        });
      }
    }
  };
})(window.ConfiguratorApp);
