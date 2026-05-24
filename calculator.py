def add(a, b):
    return a + b


def subtract(a, b):
    return a - b


def multiply(a, b):
    return a * b


def divide(a, b):
    if b == 0:
        raise ZeroDivisionError("Деление на ноль невозможно")
    return a / b


OPERATIONS = {
    "1": ("Сложение (+)", add),
    "2": ("Вычитание (-)", subtract),
    "3": ("Умножение (*)", multiply),
    "4": ("Деление (/)", divide),
}


def read_number(prompt):
    while True:
        raw = input(prompt).strip().replace(",", ".")
        try:
            return float(raw)
        except ValueError:
            print("Ошибка: введите корректное число.")


def main():
    print("=== Калькулятор ===")
    while True:
        print("\nВыберите операцию:")
        for key, (label, _) in OPERATIONS.items():
            print(f"  {key}. {label}")
        print("  0. Выход")

        choice = input("Ваш выбор: ").strip()
        if choice == "0":
            print("До свидания!")
            break
        if choice not in OPERATIONS:
            print("Неверный выбор, попробуйте снова.")
            continue

        a = read_number("Введите первое число: ")
        b = read_number("Введите второе число: ")

        label, func = OPERATIONS[choice]
        try:
            result = func(a, b)
            print(f"Результат ({label}): {result}")
        except ZeroDivisionError as e:
            print(f"Ошибка: {e}")


if __name__ == "__main__":
    main()
