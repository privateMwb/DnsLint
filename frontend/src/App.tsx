import { Route, Routes } from "react-router-dom";
import { Header } from "./components/Header";
import { CheckerPage } from "./pages/CheckerPage";
import { AboutPage } from "./pages/AboutPage";

export default function App() {
  return (
    <div className="min-h-screen">
      <Header />
      <Routes>
        <Route path="/" element={<CheckerPage />} />
        <Route path="/about" element={<AboutPage />} />
      </Routes>
    </div>
  );
}
