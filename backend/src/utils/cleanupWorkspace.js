import { rm } from 'node:fs/promises'

const cleanupWorkspace = async (workspacePath) => {
  await rm(workspacePath, { recursive: true, force:true })
}

export { cleanupWorkspace }
