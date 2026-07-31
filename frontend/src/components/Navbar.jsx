import { NavButton } from "./NavButton"
import logo from "../assets/logo.svg"

function Navbar() {
  return (
    <div className="flex justify-between">
      <div className="flex justify-center items-center">
        <img src={logo} alt="Huffly Logo" className="w-10 m-4" />
        <h1 className="m-4 ml-0 items-center text-lg font-bold">HUFF</h1>
      </div>
      <div className="p-4 flex gap-0.5 text-lg mr-4">
        <NavButton text="Home" />
        <NavButton text="Compress" />
        <NavButton text="Decompress" />
        <NavButton text="Inspect" />
        <NavButton text="About" />
      </div>
    </div>
  )
}

export { Navbar }
