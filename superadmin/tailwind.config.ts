import type { Config } from "tailwindcss";

const config: Config = {
  content: ["./app/**/*.{ts,tsx}", "./components/**/*.{ts,tsx}"],
  theme: {
    extend: {
      colors: {
        ink: { DEFAULT: "#050505", 900: "#0a0a0a", 800: "#101010", 700: "#171717", 600: "#1f1f1f" },
        accent: { DEFAULT: "#1f9d55", dark: "#14532d", soft: "#0d2b1a", glow: "#22c55e" },
      },
      fontFamily: {
        sans: ["var(--font-sans)", "ui-sans-serif", "system-ui", "sans-serif"],
        mono: ["var(--font-mono)", "ui-monospace", "monospace"],
      },
      keyframes: {
        shake: {
          "0%,100%": { transform: "translateX(0)" },
          "20%,60%": { transform: "translateX(-8px)" },
          "40%,80%": { transform: "translateX(8px)" },
        },
        scan: { "0%": { transform: "translateY(-100%)" }, "100%": { transform: "translateY(100vh)" } },
      },
      animation: { shake: "shake .4s ease-in-out", scan: "scan 7s linear infinite" },
    },
  },
  plugins: [],
};
export default config;
