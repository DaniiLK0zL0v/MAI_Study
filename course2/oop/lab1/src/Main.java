/**
 * Главный класс для демонстрации работы иерархии:
 * FinancialInstrument -> BankCard -> CreditCard
 *
 * Лабораторная работа №1 по ООП, Вариант 11 (Банковская карта).
 */
public class Main {

    public static void main(String[] args) {
        printDivider("ДЕМОНСТРАЦИЯ ЛАБОРАТОРНОЙ РАБОТЫ №1 ПО ООП (Вариант 11)");

        // ============================================================
        // 1. Базовый класс: FinancialInstrument
        // ============================================================
        printSection("1. Тестирование базового класса FinancialInstrument");
        FinancialInstrument fi = new FinancialInstrument("Иванов И.И.", "Сбербанк", "RUB");
        System.out.println("Исходное состояние:");
        System.out.println(fi.getInfo());

        System.out.println("\nРабота методов активации/блокировки:");
        fi.activate();     // уже активен
        fi.block();        // блокируем
        fi.block();        // уже заблокирован
        fi.activate();     // активируем снова

        System.out.printf("Строковое представление: %s%n", fi);
        System.out.println("Финальное состояние:");
        System.out.println(fi.getInfo());

        // ============================================================
        // 2. BankCard (Банковская карта)
        // ============================================================
        printSection("2. Тестирование класса BankCard (наследник FinancialInstrument)");
        BankCard card = new BankCard(
                "Петрова А.С.", "Тинькофф", "RUB",
                "2200123456789012", "09/28", 50000.00
        );
        System.out.println(card.getInfo());

        System.out.println("\nОперации по карте:");
        card.deposit(15000);
        card.pay(20000);
        card.pay(100000); // недостаточно средств

        System.out.printf("Маскированный номер: %s%n", card.getMaskedNumber());
        System.out.println("\nБлокируем карту и пробуем оплатить:");
        card.block();
        card.pay(100);     // карта заблокирована
        card.activate();

        System.out.println("\nФинальное состояние:");
        System.out.println(card.getInfo());

        // ============================================================
        // 3. CreditCard (Кредитная карта)
        // ============================================================
        printSection("3. Тестирование класса CreditCard (наследник BankCard)");
        CreditCard cc = new CreditCard(
                "Сидоров К.М.", "Альфа-Банк", "RUB",
                "5536914012345678", "12/27", 10000.00,
                200000.00, 23.9
        );
        System.out.println("Начальные параметры кредитной карты:");
        System.out.println(cc.getInfo());

        System.out.println("\n1) Покупка в пределах собственных средств:");
        cc.pay(5000);

        System.out.println("\n2) Крупная покупка (задействуется кредитный лимит):");
        cc.pay(50000); // 5000 собственных + 45000 кредитных

        System.out.println("\n3) Расчёт процентов по задолженности за 3 месяца:");
        double interest = cc.calculateInterest(3);
        System.out.printf("Проценты за 3 мес.: %.2f %s%n", interest, cc.getCurrency());

        System.out.println("\n4) Частичное погашение задолженности:");
        cc.repayDebt(20000);

        System.out.println("\n5) Полное погашение + пополнение:");
        cc.deposit(100000);
        cc.repayDebt(50000); // погасит остаток + излишек на баланс

        System.out.println("\nИтоговое состояние кредитной карты:");
        System.out.println(cc.getInfo());

        // ============================================================
        // 4. Полиморфизм (Upcasting)
        // ============================================================
        printSection("4. Демонстрация полиморфизма (массив FinancialInstrument[])");
        FinancialInstrument[] instruments = new FinancialInstrument[] {
                new FinancialInstrument("Козлов П.П.", "ВТБ", "USD"),
                new BankCard("Орлова М.Н.", "Газпромбанк", "RUB", "4276123412341234", "06/29", 75000),
                new CreditCard("Новиков Д.А.", "Совкомбанк", "RUB", "5100987654321098", "03/30", 5000, 150000, 19.9)
        };

        for (int i = 0; i < instruments.length; i++) {
            System.out.printf("%n[Объект #%d в массиве FinancialInstrument[]]:%n", i + 1);
            System.out.println(instruments[i].getInfo()); // Полиморфный вызов getInfo()
        }

        // ============================================================
        // 5. Валидация (обработка исключений)
        // ============================================================
        printSection("5. Проверка валидации (обработка исключений)");

        testValidation("Попытка создать карту с некорректным номером (15 цифр):", () -> {
            new BankCard("Тест", "Банк", "RUB", "123456789012345", "01/30", 0);
        });

        testValidation("Попытка задать невалидный срок действия (13/99):", () -> {
            card.setExpiryDate("13/99");
        });

        testValidation("Попытка установить кредитный лимит = 0:", () -> {
            cc.setCreditLimit(0);
        });

        testValidation("Попытка установить процентную ставку = 150%:", () -> {
            cc.setInterestRate(150);
        });

        testValidation("Попытка создать инструмент с пустым именем:", () -> {
            new FinancialInstrument("", "Банк", "RUB");
        });

        testValidation("Попытка оплатить отрицательную сумму:", () -> {
            card.pay(-100);
        });

        printDivider("ВСЕ ТЕСТЫ И ДЕМОНСТРАЦИИ УСПЕШНО ЗАВЕРШЕНЫ!");
    }

    private static void printDivider(String title) {
        System.out.println("\n" + "=".repeat(65));
        System.out.println("  " + title);
        System.out.println("=".repeat(65));
    }

    private static void printSection(String sectionName) {
        System.out.println("\n" + "-".repeat(60));
        System.out.println(">>> " + sectionName);
        System.out.println("-".repeat(60));
    }

    @FunctionalInterface
    interface RunnableWithException {
        void run() throws Exception;
    }

    private static void testValidation(String description, RunnableWithException action) {
        System.out.println(description);
        try {
            action.run();
            System.out.println("  [ОШИБКА]: Исключение не было выброшено!");
        } catch (IllegalArgumentException e) {
            System.out.println("  [УСПЕХ] Перехвачено ожидаемое IllegalArgumentException: " + e.getMessage());
        } catch (Exception e) {
            System.out.println("  [ВНИМАНИЕ] Неожиданное исключение: " + e);
        }
    }
}
