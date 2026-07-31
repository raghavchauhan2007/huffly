function NavButton({ text }) {
  return (
    <button type="button" className="hover:text-blue-400 transition-colors duration-150 active:text-blue-600 px-4 cursor-pointer">{text}</button>
  )
}

export { NavButton }
