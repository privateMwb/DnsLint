import { clsx, type ClassValue } from "clsx";
import { twMerge } from "tailwind-merge";

/**
 * Merges class lists, letting a later Tailwind class override an
 * earlier conflicting one (e.g. cn("p-4", condition && "p-2")) instead
 * of both landing in the DOM and fighting on specificity.
 */
export function cn(...inputs: ClassValue[]): string {
  return twMerge(clsx(inputs));
}
