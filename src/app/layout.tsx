import type { Metadata } from "next";
import { Fraunces, JetBrains_Mono, Onest } from "next/font/google";

import "./globals.css";

const onest = Onest({
  subsets: ["latin"],
  variable: "--font-body",
  display: "swap",
});

const fraunces = Fraunces({
  subsets: ["latin"],
  variable: "--font-display",
  display: "swap",
});

const jetbrainsMono = JetBrains_Mono({
  subsets: ["latin"],
  variable: "--font-mono",
  display: "swap",
});

export const metadata: Metadata = {
  title: "Claudefish",
  description: "Play chess against the Claudefish WebAssembly engine",
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en">
      <head>
        {/* Anti-flash: apply saved theme before React hydrates */}
        <script
          dangerouslySetInnerHTML={{
            __html: `(function(){try{var t=localStorage.getItem('theme');if(t==='dark'||(!t&&window.matchMedia('(prefers-color-scheme:dark)').matches)){document.documentElement.dataset.theme='dark';}}catch(e){}})();`,
          }}
        />
      </head>
      <body className={`${onest.variable} ${fraunces.variable} ${jetbrainsMono.variable}`}>
        {children}
      </body>
    </html>
  );
}
