/** Lee campos de un JSON desconocido y lanza nombrando el campo que no cumple. */
export class JsonObject {
  private readonly fields: Readonly<Record<string, unknown>>;

  constructor(data: unknown) {
    if (typeof data !== 'object' || data === null || Array.isArray(data)) {
      throw new Error('se esperaba un objeto');
    }
    this.fields = data as Readonly<Record<string, unknown>>;
  }

  /** Valor sin validar, para entregarlo al parser de un objeto anidado. */
  value(key: string): unknown {
    return this.fields[key];
  }

  string(key: string): string {
    const value = this.fields[key];
    if (typeof value !== 'string') {
      throw new Error(`el campo "${key}" debe ser texto`);
    }
    return value;
  }

  number(key: string): number {
    const value = this.fields[key];
    if (typeof value !== 'number') {
      throw new Error(`el campo "${key}" debe ser un número`);
    }
    return value;
  }

  array(key: string): readonly unknown[] {
    const value = this.fields[key];
    if (!Array.isArray(value)) {
      throw new Error(`el campo "${key}" debe ser una lista`);
    }
    return value as readonly unknown[];
  }

  stringArray(key: string): readonly string[] {
    return this.array(key).map((item) => {
      if (typeof item !== 'string') {
        throw new Error(`el campo "${key}" debe ser una lista de textos`);
      }
      return item;
    });
  }

  oneOf<T extends string>(key: string, allowed: readonly T[]): T {
    const value = this.string(key);
    const match = allowed.find((option) => option === value);
    if (match === undefined) {
      throw new Error(`el campo "${key}" no admite el valor "${value}"`);
    }
    return match;
  }
}
