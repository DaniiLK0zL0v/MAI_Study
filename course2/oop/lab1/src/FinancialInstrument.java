import java.util.Objects;

/**
 * Класс 1: Финансовый инструмент (FinancialInstrument) — базовый класс.
 * Описывает любое платёжное/финансовое средство.
 *
 * Собственные атрибуты:
 * - holderName (String)     — имя владельца инструмента
 * - issuingBank (String)    — банк-эмитент
 * - currency (String)       — валюта (например, "RUB", "USD")
 * - isActive (boolean)      — активен ли инструмент
 */
public class FinancialInstrument {

    private String holderName;
    private String issuingBank;
    private String currency;
    private boolean isActive;

    /**
     * Конструктор базового класса.
     *
     * @param holderName  имя владельца (непустая строка)
     * @param issuingBank банк-эмитент (непустая строка)
     * @param currency    код валюты (непустая строка)
     */
    public FinancialInstrument(String holderName, String issuingBank, String currency) {
        setHolderName(holderName);
        setIssuingBank(issuingBank);
        setCurrency(currency);
        this.isActive = true;
    }

    // ============================================================
    // Геттеры и Сеттеры
    // ============================================================

    public String getHolderName() {
        return holderName;
    }

    public void setHolderName(String holderName) {
        if (holderName == null || holderName.trim().isEmpty()) {
            throw new IllegalArgumentException("Имя владельца не может быть пустым.");
        }
        this.holderName = holderName.trim();
    }

    public String getIssuingBank() {
        return issuingBank;
    }

    public void setIssuingBank(String issuingBank) {
        if (issuingBank == null || issuingBank.trim().isEmpty()) {
            throw new IllegalArgumentException("Название банка-эмитента не может быть пустым.");
        }
        this.issuingBank = issuingBank.trim();
    }

    public String getCurrency() {
        return currency;
    }

    public void setCurrency(String currency) {
        if (currency == null || currency.trim().isEmpty()) {
            throw new IllegalArgumentException("Код валюты не может быть пустым.");
        }
        this.currency = currency.trim().toUpperCase();
    }

    public boolean isActive() {
        return isActive;
    }

    public void setActive(boolean active) {
        this.isActive = active;
    }

    // ============================================================
    // Собственные методы (не менее 3, геттеры/сеттеры не в счёт)
    // ============================================================

    /**
     * Метод 1: Активация инструмента.
     */
    public void activate() {
        if (this.isActive) {
            System.out.println("[" + getShortName() + "] Инструмент уже активен.");
        } else {
            this.isActive = true;
            System.out.println("[" + getShortName() + "] Инструмент активирован.");
        }
    }

    /**
     * Метод 2: Блокировка (деактивация) инструмента.
     */
    public void block() {
        if (!this.isActive) {
            System.out.println("[" + getShortName() + "] Инструмент уже заблокирован.");
        } else {
            this.isActive = false;
            System.out.println("[" + getShortName() + "] Инструмент заблокирован.");
        }
    }

    /**
     * Метод 3: Формирование текстового описания инструмента.
     */
    public String getInfo() {
        String status = isActive ? "Активен" : "Заблокирован";
        return String.format("Фин. инструмент: %s | Банк: %s | Валюта: %s | Статус: %s",
                holderName, issuingBank, currency, status);
    }

    /**
     * Вспомогательный метод для компактного имени в логах.
     */
    public String getShortName() {
        return issuingBank + "/" + holderName;
    }

    @Override
    public String toString() {
        return getShortName();
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof FinancialInstrument)) return false;
        FinancialInstrument that = (FinancialInstrument) o;
        return isActive == that.isActive &&
                Objects.equals(holderName, that.holderName) &&
                Objects.equals(issuingBank, that.issuingBank) &&
                Objects.equals(currency, that.currency);
    }

    @Override
    public int hashCode() {
        return Objects.hash(holderName, issuingBank, currency, isActive);
    }
}
