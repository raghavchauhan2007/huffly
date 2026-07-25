import { handleError } from "./handleError"

const withErrorHandler = (handler) => (req) => Promise.resolve(handler(req)).catch((error) => handleError(error))

export { withErrorHandler }
