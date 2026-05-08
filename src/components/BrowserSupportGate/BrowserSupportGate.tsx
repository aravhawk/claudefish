"use client";

import { useBrowserSupport } from "@/hooks/useBrowserSupport";

import styles from "./BrowserSupportGate.module.css";

interface BrowserSupportGateProps {
  /** Rendered when the browser passes all checks */
  children: React.ReactNode;
}

export default function BrowserSupportGate({ children }: BrowserSupportGateProps) {
  const { checking, supported, reason, detected } = useBrowserSupport();

  // Still checking — show nothing (or a brief flash)
  if (checking) {
    return null;
  }

  if (supported) {
    return <>{children}</>;
  }

  const checks = [
    {
      label: "Device",
      value: detected.isMobile ? "Mobile" : "Desktop",
      ok: !detected.isMobile,
    },
    {
      label: "WebAssembly",
      value: detected.hasWasm ? "Available" : "Missing",
      ok: detected.hasWasm,
    },
    {
      label: "SharedArrayBuffer",
      value: detected.hasSharedArrayBuffer ? "Available" : "Missing",
      ok: detected.hasSharedArrayBuffer,
    },
    {
      label: "Secure Context",
      value: detected.hasCoopCoep ? "Isolated" : "Not isolated",
      ok: detected.hasCoopCoep,
    },
    ...(detected.browser
      ? [{ label: "Browser", value: detected.browser, ok: true }]
      : []),
  ];

  return (
    <div className={styles.gate} role="alert">
      <div className={styles.gateCard}>
        <div aria-hidden="true" className={styles.gateIcon}>
          &#9888;
        </div>

        <h2 className={styles.gateTitle}>Unsupported Browser</h2>

        {reason && <p className={styles.gateMessage}>{reason}</p>}

        <div className={styles.gateDetails}>
          {checks.map((check) => (
            <div className={styles.detailRow} key={check.label}>
              <span className={styles.detailLabel}>{check.label}</span>
              <span
                className={`${styles.detailValue} ${
                  check.ok ? styles.detailValueOk : styles.detailValueFail
                }`}
              >
                {check.value}
              </span>
            </div>
          ))}
        </div>

        <footer className={styles.gateFooter}>
          <p style={{ margin: 0, fontSize: "0.72rem", color: "var(--theme-text-soft)" }}>
            Claudefish requires a modern desktop browser with WebAssembly and
            SharedArrayBuffer support.
          </p>

          <div className={styles.supportedBrowsers}>
            {["Chrome 91+", "Edge 91+", "Firefox 89+", "Safari 16.4+"].map(
              (browser) => (
                <span className={styles.browserTag} key={browser}>
                  {browser}
                </span>
              ),
            )}
          </div>
        </footer>
      </div>
    </div>
  );
}
